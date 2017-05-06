/*******************************************************************\

Module: Main Module

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <ac_config.h>

#ifndef _WIN32
extern "C" {
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_SENDFILE_ESBMC
#include <sys/sendfile.h>
#endif

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
}
#endif

#include <esbmc/bmc.h>
#include <esbmc/esbmc_parseoptions.h>
#include <ansi-c/c_preprocess.h>
#include <cctype>
#include <clang-c-frontend/clang_c_language.h>
#include <util/config.h>
#include <csignal>
#include <cstdlib>
#include <util/expr_util.h>
#include <fstream>
#include <goto-programs/add_race_assertions.h>
#include <goto-programs/goto_check.h>
#include <goto-programs/goto_convert_functions.h>
#include <goto-programs/goto_inline.h>
#include <goto-programs/goto_k_induction.h>
#include <goto-programs/goto_unwind.h>
#include <goto-programs/loop_numbers.h>
#include <goto-programs/read_goto_binary.h>
#include <goto-programs/remove_skip.h>
#include <goto-programs/remove_unreachable.h>
#include <goto-programs/set_claims.h>
#include <goto-programs/show_claims.h>
#include <util/irep.h>
#include <langapi/languages.h>
#include <langapi/mode.h>
#include <memory>
#include <pointer-analysis/goto_program_dereference.h>
#include <pointer-analysis/show_value_sets.h>
#include <pointer-analysis/value_set_analysis.h>
#include <util/symbol.h>
#include <sys/wait.h>
#include <util/time_stopping.h>

enum PROCESS_TYPE { BASE_CASE, FORWARD_CONDITION, INDUCTIVE_STEP, PARENT };

struct resultt
{
  PROCESS_TYPE type;
  u_int k;
};

#ifndef _WIN32
void
timeout_handler(int dummy __attribute__((unused)))
{
  std::cout << "Timed out" << std::endl;

  // Unfortunately some highly useful pieces of code hook themselves into
  // aexit and attempt to free some memory. That doesn't really make sense to
  // occur on exit, but more importantly doesn't mix well with signal handlers,
  // and results in the allocator locking against itself. So use _exit instead
  _exit(1);
}
#endif

void cbmc_parseoptionst::set_verbosity_msg(messaget &message)
{
  int v=8;

  if(cmdline.isset("verbosity"))
  {
    v=atoi(cmdline.getval("verbosity"));
    if(v<0)
      v=0;
    else if(v>9)
      v=9;
  }

  message.set_verbosity(v);
}

extern "C" uint8_t *esbmc_version_string;

uint64_t cbmc_parseoptionst::read_time_spec(const char *str)
{
  uint64_t mult;
  int len = strlen(str);
  if (!isdigit(str[len-1])) {
    switch (str[len-1]) {
    case 's':
      mult = 1;
      break;
    case 'm':
      mult = 60;
      break;
    case 'h':
      mult = 3600;
      break;
    case 'd':
      mult = 86400;
      break;
    default:
      std::cerr << "Unrecognized timeout suffix" << std::endl;
      abort();
    }
  } else {
    mult = 1;
  }

  uint64_t timeout = strtol(str, NULL, 10);
  timeout *= mult;
  return timeout;
}

uint64_t cbmc_parseoptionst::read_mem_spec(const char *str)
{

  uint64_t mult;
  int len = strlen(str);
  if (!isdigit(str[len-1])) {
    switch (str[len-1]) {
    case 'b':
      mult = 1;
      break;
    case 'k':
      mult = 1024;
      break;
    case 'm':
      mult = 1024*1024;
      break;
    case 'g':
      mult = 1024*1024*1024;
      break;
    default:
      std::cerr << "Unrecognized memlimit suffix" << std::endl;
      abort();
    }
  } else {
    mult = 1024*1024;
  }

  uint64_t size = strtol(str, NULL, 10);
  size *= mult;
  return size;
}

void cbmc_parseoptionst::get_command_line_options(optionst &options)
{
  if(config.set(cmdline))
  {
    exit(1);
  }

  options.cmdline(cmdline);

  /* graphML generation options check */
  if(cmdline.isset("witness-output"))
    options.set_option("witness-output", cmdline.getval("witness-output"));

  if(cmdline.isset("witness-detailed"))
    options.set_option("witness-detailed", true);

  if(cmdline.isset("git-hash"))
  {
    std::cout << esbmc_version_string << std::endl;
    exit(0);
  }

  if(cmdline.isset("list-solvers"))
  {
    // Generated for us by autoconf,
    std::cout << "Available solvers: " << ESBMC_AVAILABLE_SOLVERS << std::endl;
    exit(0);
  }

  if(cmdline.isset("bv"))
  {
    options.set_option("int-encoding", false);
  }

  if(cmdline.isset("ir"))
  {
    options.set_option("int-encoding", true);
  }

  options.set_option("fixedbv", true);

  if(cmdline.isset("context-bound"))
    options.set_option("context-bound", cmdline.getval("context-bound"));
  else
    options.set_option("context-bound", -1);

  if(cmdline.isset("lock-order-check"))
    options.set_option("lock-order-check", true);

  if(cmdline.isset("deadlock-check"))
  {
    options.set_option("deadlock-check", true);
    options.set_option("atomicity-check", false);
  }
  else
    options.set_option("deadlock-check", false);

  if(cmdline.isset("smt-during-symex"))
  {
    std::cout << "Enabling --no-slice due to presence of --smt-during-symex";
    std::cout << std::endl;
    options.set_option("no-slice", true);
  }

  if(cmdline.isset("smt-thread-guard") || cmdline.isset("smt-symex-guard"))
  {
    if(!cmdline.isset("smt-during-symex"))
    {
      std::cerr << "Please explicitly specify --smt-during-symex if you want "
          "to use features that involve encoding SMT during symex" << std::endl;
      abort();
    }
  }

  if(cmdline.isset("base-case"))
  {
    options.set_option("base-case", true);
    options.set_option("no-unwinding-assertions", true);
    options.set_option("partial-loops", false);
  }

  if(cmdline.isset("forward-condition"))
  {
    options.set_option("forward-condition", true);
    options.set_option("no-unwinding-assertions", false);
    options.set_option("partial-loops", false);
    options.set_option("no-assertions", true);
  }

  if(cmdline.isset("inductive-step"))
  {
    options.set_option("inductive-step", true);
    options.set_option("no-unwinding-assertions", true);
    options.set_option("partial-loops", true);
  }

  if(cmdline.isset("timeout"))
  {
#ifdef _WIN32
    std::cerr << "Timeout unimplemented on Windows, sorry" << std::endl;
    abort();
#else
    const char *time = cmdline.getval("timeout");
    uint64_t timeout = read_time_spec(time);
    signal(SIGALRM, timeout_handler);
    alarm(timeout);
#endif
  }

  if(cmdline.isset("memlimit"))
  {
#ifdef _WIN32
    std::cerr << "Can't memlimit on Windows, sorry" << std::endl;
    abort();
#else
    uint64_t size = read_mem_spec(cmdline.getval("memlimit"));

    struct rlimit lim;
    lim.rlim_cur = size;
    lim.rlim_max = size;
    if(setrlimit(RLIMIT_DATA, &lim) != 0)
    {
      perror("Couldn't set memory limit");
      abort();
    }
#endif
  }

#ifndef _WIN32
  struct rlimit lim;
  if(cmdline.isset("enable-core-dump"))
  {
    lim.rlim_cur = RLIM_INFINITY;
    lim.rlim_max = RLIM_INFINITY;
    if(setrlimit(RLIMIT_CORE, &lim) != 0)
    {
      perror("Couldn't unlimit core dump size");
      abort();
    }
  }
  else
  {
    lim.rlim_cur = 0;
    lim.rlim_max = 0;
    if(setrlimit(RLIMIT_CORE, &lim) != 0)
    {
      perror("Couldn't disable core dump size");
      abort();
    }
  }
#endif

  if(cmdline.isset("keep-unused"))
    options.set_option("keep-unused", true);

  config.options = options;
}

int cbmc_parseoptionst::doit()
{
  //
  // Print a banner
  //
  std::cout << "ESBMC version " << ESBMC_VERSION " "
            << sizeof(void *)*8 << "-bit "
            << config.this_architecture() << " "
            << config.this_operating_system() << std::endl;

  if(cmdline.isset("version"))
    return 0;

  //
  // unwinding of transition systems
  //

  if(cmdline.isset("module") ||
    cmdline.isset("gen-interface"))

  {
    error("This version has no support for "
          " hardware modules.");
    return 1;
  }

  //
  // command line options
  //

  set_verbosity_msg(*this);

  if(cmdline.isset("preprocess"))
  {
    preprocessing();
    return 0;
  }

  if(cmdline.isset("k-induction"))
    return doit_k_induction();

  if(cmdline.isset("k-induction-parallel"))
    return doit_k_induction_parallel();

  if(cmdline.isset("falsification"))
    return doit_falsification();

  if(cmdline.isset("incremental-bmc"))
    return doit_incremental();

  optionst opts;
  get_command_line_options(opts);

  if(get_goto_program(opts, goto_functions))
    return 6;

  if(cmdline.isset("show-claims"))
  {
    const namespacet ns(context);
    show_claims(ns, get_ui(), goto_functions);
    return 0;
  }

  if(set_claims(goto_functions))
    return 7;

  if (opts.get_bool_option("skip-bmc"))
    return 0;

  bool res = false;

  // do actual BMC
  bmct bmc(goto_functions, opts, context, ui_message_handler);
  set_verbosity_msg(bmc);
  res = do_bmc(bmc);

  return res;
}

int cbmc_parseoptionst::doit_k_induction_parallel()
{
  // Pipes for communication between processes
  int forward_pipe[2], backward_pipe[2];

  // Process type
  PROCESS_TYPE process_type = PARENT;

  if(pipe(forward_pipe))
  {
    status("\nPipe Creation Failed, giving up.");
    _exit(1);
  }

  if(pipe(backward_pipe))
  {
    status("\nPipe Creation Failed, giving up.");
    _exit(1);
  }

  /* Set file descriptor non-blocking */
  fcntl(backward_pipe[0], F_SETFL, fcntl(backward_pipe[0], F_GETFL) | O_NONBLOCK);

  pid_t children_pid[3];
  short num_p = 0;

  // We need to fork 3 times: one for each step
  for(u_int p = 0; p < 3; ++p)
  {
    pid_t pid = fork();

    if(pid == -1)
    {
      status("\nFork Failed, giving up.");
      _exit(1);
    }

    // Child process
    if(!pid)
    {
      process_type = PROCESS_TYPE(p);
      break;
    }
    else // Parent process
    {
      children_pid[p] = pid;
      ++num_p;
    }
  }

  if(process_type == PARENT && num_p != 3)
  {
    std::cerr << "Child processes were not created sucessfully." << std::endl;
    abort();
  }

  optionst opts;

  if(process_type != PARENT)
  {
    // Get full set of options
    get_command_line_options(opts);

    // Generate goto functions and set claims
    if(get_goto_program(opts, goto_functions))
      return 6;

    if(cmdline.isset("show-claims"))
    {
      const namespacet ns(context);
      show_claims(ns, get_ui(), goto_functions);
      return 0;
    }

    if(set_claims(goto_functions))
      return 7;
  }

  // Get max number of iterations
  u_int max_k_step = strtoul(cmdline.getval("max-k-step"), nullptr, 10);

  // The option unlimited-k-steps set the max number of iterations to UINT_MAX
  if(cmdline.isset("unlimited-k-steps"))
    max_k_step = UINT_MAX;

  // Get the increment
  unsigned k_step_inc = strtoul(cmdline.getval("k-step"), nullptr, 10);

  // All processes were created successfully
  switch(process_type)
  {
    case PARENT:
    {
      // Communication to child processes
      close(forward_pipe[1]);
      close(backward_pipe[0]);

      struct resultt a_result;
      bool bc_finished = false, fc_finished = false, is_finished = false;
      u_int bc_solution = max_k_step, fc_solution = max_k_step, is_solution = max_k_step;

      // Keep reading until we find an answer
      while(!(bc_finished && fc_finished && is_finished))
      {
        // Perform read and interpret the number of bytes read
        int read_size = read(forward_pipe[0], &a_result, sizeof(resultt));
        if(read_size != sizeof(resultt))
        {
          if(read_size == 0)
          {
            // Client hung up; continue on, but don't interpret the result.
            ;
          }
          else
          {
            // Invalid size read.
            std::cerr << "Short read communicating with kinduction children"
                << std::endl;
            std::cerr << "Size " << read_size << ", expected "
                << sizeof(resultt) << std::endl;
            abort();
          }
        }

        // Eventually the parent process will check if the child process is alive

        // Check base case process
        if(!bc_finished)
        {
          int status;
          pid_t result = waitpid(children_pid[0], &status, WNOHANG);
          if(result == 0)
          {
            // Child still alive
          }
          else if(result == -1)
          {
            // Error
          }
          else
          {
            std::cout << "**** WARNING: Base case process crashed." << std::endl;

            bc_finished = true;
            if(cmdline.isset("dont-ignore-dead-child-process"))
              fc_finished = is_finished = true;
          }
        }

        // Check forward condition process
        if(!fc_finished)
        {
          int status;
          pid_t result = waitpid(children_pid[1], &status, WNOHANG);
          if(result == 0)
          {
            // Child still alive
          }
          else if(result == -1)
          {
            // Error
          }
          else
          {
            std::cout << "**** WARNING: Forward condition process crashed." << std::endl;

            fc_finished = true;
            if(cmdline.isset("dont-ignore-dead-child-process"))
              bc_finished = is_finished = true;
          }
        }

        // Check inductive step process
        if(!is_finished)
        {
          int status;
          pid_t result = waitpid(children_pid[2], &status, WNOHANG);
          if(result == 0)
          {
            // Child still alive
          }
          else if(result == -1)
          {
            // Error
          }
          else
          {
            std::cout << "**** WARNING: Inductive step process crashed." << std::endl;

            is_finished = true;
            if(cmdline.isset("dont-ignore-dead-child-process"))
              bc_finished = fc_finished = true;
          }
        }

        switch(a_result.type)
        {
          case BASE_CASE:
            bc_finished = true;
            bc_solution = a_result.k;
            break;

          case FORWARD_CONDITION:
            fc_finished = true;
            fc_solution = a_result.k;
            break;

          case INDUCTIVE_STEP:
            is_finished = true;
            is_solution = a_result.k;
            break;

          default:
            std::cerr << "Message from unrecognized k-induction child "
                << "process" << std::endl;
            abort();
        }

        // If either the base case found a bug or the forward condition
        // finds a solution, present the result
        if(bc_finished && (bc_solution != 0) && (bc_solution != max_k_step))
          break;

        // If the either the forward condition or inductive step finds a
        // solution, first check if base case couldn't find a bug in that code,
        // if there is no bug, inductive step can present the result

        if(fc_finished && (fc_solution != 0) && (fc_solution != max_k_step))
        {
          // If base case finished, then we can present the result
          if(bc_finished)
            break;

          // Otherwise, ask base case for a solution

          // Struct to keep the result
          struct resultt r = { process_type, 0 };

          r.k = fc_solution;

          // Write result
          u_int len = write(backward_pipe[1], &r, sizeof(r));
          assert(len == sizeof(r) && "short write");
          (void)len; //ndebug
        }

        if(is_finished && (is_solution != 0) && (is_solution != max_k_step))
        {
          // If base case finished, then we can present the result
          if(bc_finished)
            break;

          // Otherwise, ask base case for a solution

          // Struct to keep the result
          struct resultt r = { process_type, 0 };

          r.k = is_solution;

          // Write result
          u_int len = write(backward_pipe[1], &r, sizeof(r));
          assert(len == sizeof(r) && "short write");
          (void)len; //ndebug
        }
      }

      for(short i = 0; i < 3; ++i)
        kill(children_pid[i], SIGKILL);

      // Check if a solution was found by the base case
      if(bc_finished && (bc_solution != 0) && (bc_solution != max_k_step))
      {
        std::cout << std::endl << "Bug found by the base case (k = "
            << bc_solution << ")" << std::endl;
        std::cout << "VERIFICATION FAILED" << std::endl;
        return true;
      }

      // Check if a solution was found by the forward condition
      if(fc_finished && (fc_solution != 0) && (fc_solution != max_k_step))
      {
        // We should only present the result if the base case finished
        // and haven't crashed (if it crashed, bc_solution will be UINT_MAX
        if(bc_finished && (bc_solution != max_k_step))
        {
          std::cout << std::endl << "Solution found by the forward condition; "
              << "all states are reachable (k = " << fc_solution
              << ")" << std::endl;
          std::cout << "VERIFICATION SUCCESSFUL" << std::endl;
          return false;
        }
      }

      // Check if a solution was found by the inductive step
      if(is_finished && (is_solution != 0) && (is_solution != max_k_step))
      {
        // We should only present the result if the base case finished
        // and haven't crashed (if it crashed, bc_solution will be UINT_MAX
        if(bc_finished && (bc_solution != max_k_step))
        {
          std::cout << std::endl << "Solution found by the inductive step "
              << "(k = " << is_solution << ")" << std::endl;
          std::cout << "VERIFICATION SUCCESSFUL" << std::endl;
          return false;
        }
      }

      // Couldn't find a bug or a proof for the current deepth
      std::cout << std::endl << "VERIFICATION UNKNOWN" << std::endl;
      return false;
      break;
    }

    case BASE_CASE:
    {
      // Set that we are running base case
      opts.set_option("base-case", true);
      opts.set_option("forward-condition", false);
      opts.set_option("inductive-step", false);

      // Start communication to the parent process
      close(forward_pipe[0]);
      close(backward_pipe[1]);

      // Struct to keep the result
      struct resultt r = { process_type, 0 };

      // Run bmc and only send results in two occasions:
      // 1. A bug was found, we send the step where it was found
      // 2. It couldn't find a bug
      for(u_int k_step = 1; k_step <= max_k_step; k_step += k_step_inc)
      {
        bmct bmc(goto_functions, opts, context, ui_message_handler);
        set_verbosity_msg(bmc);

        bmc.options.set_option("unwind", i2string(k_step));

        std::cout << std::endl << "*** K-Induction Loop Iteration ";
        std::cout << i2string((unsigned long) k_step);
        std::cout << " ***" << std::endl;
        std::cout << "*** Checking base case" << std::endl;

        // If an exception was thrown, we should abort the process
        bool res = true;
        try {
          res = do_bmc(bmc);
        }
        catch(...)
        {
          break;
        }

        // Send information to parent if no bug was found
        if(res)
        {
          r.k = k_step;

          // Write result
          u_int len = write(forward_pipe[1], &r, sizeof(r));
          assert(len == sizeof(r) && "short write");
          (void)len; //ndebug

          std::cout << "BASE CASE PROCESS FINISHED." << std::endl;

          return 1;
        }

        // Check if the parent process is asking questions

        // Perform read and interpret the number of bytes read
        struct resultt a_result;
        int read_size = read(backward_pipe[0], &a_result, sizeof(resultt));
        if(read_size != sizeof(resultt))
        {
          if(read_size == 0)
          {
            // Client hung up; continue on, but don't interpret the result.
            continue;
          }
          else if (read_size == -1 && errno == EAGAIN)
          {
            // No data available yet
            continue;
          }
          else
          {
            // Invalid size read.
            std::cerr << "Short read communicating with kinduction parent"
                << std::endl;
            std::cerr << "Size " << read_size << ", expected "
                << sizeof(resultt) << std::endl;
            abort();
          }
        }

        // We only receive messages from the parent
        assert(a_result.type == PARENT);

        // If the value being asked is greater or equal the current step,
        // then we can stop the base case. It can be equal, because we
        // have just checked the current value of k

        if(a_result.k >= k_step)
          break;
        else
        {
          // Otherwise, we just need to check the base case for k = a_result.k
          k_step = max_k_step = a_result.k;
        }
      }

      // Send information to parent that a bug was not found
      r.k = 0;

      u_int len = write(forward_pipe[1], &r, sizeof(r));
      assert(len == sizeof(r) && "short write");
      (void)len; //ndebug

      std::cout << "BASE CASE PROCESS FINISHED." << std::endl;
      break;
    }

    case FORWARD_CONDITION:
    {
      // Set that we are running forward condition
      opts.set_option("base-case", false);
      opts.set_option("forward-condition", true);
      opts.set_option("inductive-step", false);

      // Start communication to the parent process
      close(forward_pipe[0]);
      close(backward_pipe[1]);

      // Struct to keep the result
      struct resultt r = { process_type, 0 };

      // Run bmc and only send results in two occasions:
      // 1. A proof was found, we send the step where it was found
      // 2. It couldn't find a proof
      for(u_int k_step = 2; k_step <= max_k_step; k_step += k_step_inc)
      {
        if(opts.get_bool_option("disable-forward-condition"))
          break;

        bmct bmc(goto_functions, opts, context, ui_message_handler);
        set_verbosity_msg(bmc);

        bmc.options.set_option("unwind", i2string(k_step));

        std::cout << std::endl << "*** K-Induction Loop Iteration ";
        std::cout << i2string((unsigned long) k_step);
        std::cout << " ***" << std::endl;
        std::cout << "*** Checking forward condition" << std::endl;

        // If an exception was thrown, we should abort the process
        bool res = true;
        try {
          res = do_bmc(bmc);
        }
        catch(...)
        {
          break;
        }

        // Send information to parent if no bug was found
        if(!res)
        {
          r.k = k_step;

          // Write result
          u_int len = write(forward_pipe[1], &r, sizeof(r));
          assert(len == sizeof(r) && "short write");
          (void)len; //ndebug

          std::cout << "FORWARD CONDITION PROCESS FINISHED." << std::endl;

          return 0;
        }
      }

      // Send information to parent that it couldn't prove the code
      r.k = 0;

      u_int len = write(forward_pipe[1], &r, sizeof(r));
      assert(len == sizeof(r) && "short write");
      (void)len; //ndebug

      std::cout << "FORWARD CONDITION PROCESS FINISHED." << std::endl;
      break;
    }

    case INDUCTIVE_STEP:
    {
      // Set that we are running inductive step
      opts.set_option("base-case", false);
      opts.set_option("forward-condition", false);
      opts.set_option("inductive-step", true);

      // This will be changed to true if the code contains:
      // 1. Dynamic allocated memory
      // 2. Multithreaded code (during symbolic execution)
      // 3. Recursion (during inlining)
      opts.set_option("disable-inductive-step", false);

      // Start communication to the parent process
      close(forward_pipe[0]);
      close(backward_pipe[1]);

      // Struct to keep the result
      struct resultt r = { process_type, 0 };

      // Run bmc and only send results in two occasions:
      // 1. A proof was found, we send the step where it was found
      // 2. It couldn't find a proof
      for(u_int k_step = 2; k_step <= max_k_step; k_step += k_step_inc)
      {
        bmct bmc(goto_functions, opts, context, ui_message_handler);
        set_verbosity_msg(bmc);

        bmc.options.set_option("unwind", i2string(k_step));

        std::cout << std::endl << "*** K-Induction Loop Iteration ";
        std::cout << i2string((unsigned long) k_step+1);
        std::cout << " ***" << std::endl;
        std::cout << "*** Checking inductive step" << std::endl;

        // If an exception was thrown, we should abort the process
        bool res = true;
        try {
          res = do_bmc(bmc);
        }
        catch(...)
        {
          break;
        }

        // Send information to parent if no bug was found
        if(!res)
        {
          r.k = k_step;

          // Write result
          u_int len = write(forward_pipe[1], &r, sizeof(r));
          assert(len == sizeof(r) && "short write");
          (void)len; //ndebug

          std::cout << "INDUCTIVE STEP PROCESS FINISHED." << std::endl;

          return res;
        }
      }

      // Send information to parent that it couldn't prove the code
      r.k = 0;

      u_int len = write(forward_pipe[1], &r, sizeof(r));
      assert(len == sizeof(r) && "short write");
      (void)len; //ndebug

      std::cout << "INDUCTIVE STEP PROCESS FINISHED." << std::endl;
      break;
    }

    default:
      assert(0 && "Unknown process type.");
  }

  return 0;
}

int cbmc_parseoptionst::doit_k_induction()
{
  // Generate goto functions for base case and forward condition
  status("\n*** Generating Base Case and Forward Condition ***");

  optionst opts;
  get_command_line_options(opts);

  if(get_goto_program(opts, goto_functions))
    return 6;

  if(cmdline.isset("show-claims"))
  {
    const namespacet ns(context);
    show_claims(ns, get_ui(), goto_functions);
    return 0;
  }

  if(set_claims(goto_functions))
    return 7;

  // Get max number of iterations
  u_int max_k_step = strtoul(cmdline.getval("max-k-step"), nullptr, 10);

  // The option unlimited-k-steps set the max number of iterations to UINT_MAX
  if(cmdline.isset("unlimited-k-steps"))
    max_k_step = UINT_MAX;

  // Get the increment
  unsigned k_step_inc = strtoul(cmdline.getval("k-step"), nullptr, 10);

  for(unsigned long k_step = 1; k_step <= max_k_step; k_step += k_step_inc)
  {
    std::cout << "\n*** K-Induction Loop Iteration ";
    std::cout << i2string((unsigned long) k_step);
    std::cout << " ***\n";
    std::cout << "*** Checking base case\n";

    if(do_base_case(opts, goto_functions, k_step))
      return true;

    std::cout << "\n*** K-Induction Loop Iteration ";
    std::cout << i2string((unsigned long) k_step);
    std::cout << " ***\n";
    std::cout << "*** Checking forward condition\n";

    if(!do_forward_condition(opts, goto_functions, k_step))
      return false;

    if(k_step > 1)
    {
      std::cout << "\n*** K-Induction Loop Iteration ";
      std::cout << i2string((unsigned long) k_step);
      std::cout << " ***\n";
      std::cout << "*** Checking inductive step\n";
    }

    if(!do_inductive_step(opts, goto_functions, k_step))
      return false;
  }

  status("Unable to prove or falsify the program, giving up.");
  status("VERIFICATION UNKNOWN");

  return 0;
}

int cbmc_parseoptionst::doit_falsification()
{
  // Generate goto functions for base case and forward condition
  goto_functionst goto_functions;

  optionst opts;
  get_command_line_options(opts);

  if(get_goto_program(opts, goto_functions))
    return 6;

  if(cmdline.isset("show-claims"))
  {
    const namespacet ns(context);
    show_claims(ns, get_ui(), goto_functions);
    return 0;
  }

  if(set_claims(goto_functions))
    return 7;

  // Get max number of iterations
  u_int max_k_step = strtoul(cmdline.getval("max-k-step"), nullptr, 10);

  // The option unlimited-k-steps set the max number of iterations to UINT_MAX
  if(cmdline.isset("unlimited-k-steps"))
    max_k_step = UINT_MAX;

  // Get the increment
  unsigned k_step_inc = strtoul(cmdline.getval("k-step"), nullptr, 10);

  for(unsigned long k_step = 1; k_step <= max_k_step; k_step += k_step_inc)
  {
    std::cout << "\n*** Iteration number ";
    std::cout << i2string((unsigned long) k_step);
    std::cout << " ***\n";

    if(do_base_case(opts, goto_functions, k_step))
      return true;
  }

  status("Unable to prove or falsify the program, giving up.");
  status("VERIFICATION UNKNOWN");

  return 0;
}

int cbmc_parseoptionst::doit_incremental()
{
  // Generate goto functions for base case and forward condition

  optionst opts;
  get_command_line_options(opts);

  if(get_goto_program(opts, goto_functions))
    return 6;

  if(cmdline.isset("show-claims"))
  {
    const namespacet ns(context);
    show_claims(ns, get_ui(), goto_functions);
    return 0;
  }

  if(set_claims(goto_functions))
    return 7;

  // Get max number of iterations
  u_int max_k_step = strtoul(cmdline.getval("max-k-step"), nullptr, 10);

  // The option unlimited-k-steps set the max number of iterations to UINT_MAX
  if(cmdline.isset("unlimited-k-steps"))
    max_k_step = UINT_MAX;

  // Get the increment
  unsigned k_step_inc = strtoul(cmdline.getval("k-step"), nullptr, 10);

  for(unsigned long k_step = 1; k_step <= max_k_step; k_step += k_step_inc)
  {
    std::cout << "\n*** Iteration number ";
    std::cout << i2string((unsigned long) k_step);
    std::cout << " ***\n";

    if(do_base_case(opts, goto_functions, k_step))
      return true;

    if(!do_forward_condition(opts, goto_functions, k_step))
      return false;
  }

  status("Unable to prove or falsify the program, giving up.");
  status("VERIFICATION UNKNOWN");

  return 0;
}

int cbmc_parseoptionst::do_base_case(
  optionst &opts, goto_functionst &goto_functions, int k_step)
{
  opts.set_option("base-case", true);
  opts.set_option("forward-condition", false);
  opts.set_option("inductive-step", false);

  bmct bmc(goto_functions, opts, context, ui_message_handler);
  set_verbosity_msg(bmc);

  bmc.options.set_option("unwind", i2string(k_step));

  if(do_bmc(bmc))
  {
    std::cout << "\nBug found at k = " << k_step << "\n";
    return true;
  }

  return false;
}

int cbmc_parseoptionst::do_forward_condition(
  optionst &opts, goto_functionst &goto_functions, int k_step)
{
  if(opts.get_bool_option("disable-forward-condition"))
    return true;

  opts.set_option("base-case", false);
  opts.set_option("forward-condition", true);
  opts.set_option("inductive-step", false);

  bmct bmc(goto_functions, opts, context, ui_message_handler);
  set_verbosity_msg(bmc);

  bmc.options.set_option("unwind", i2string(k_step));

  if(!do_bmc(bmc))
  {
    std::cout << "\nSolution found by the forward condition; "
        << "all states are reachable (k = " << k_step << ")\n";
    return false;
  }

  return true;
}

int cbmc_parseoptionst::do_inductive_step(
  optionst &opts, goto_functionst &goto_functions, int k_step)
{
  // Don't run inductive step for k_step == 1
  if(k_step == 1)
    return true;

  if(opts.get_bool_option("disable-inductive-step"))
    return true;

  opts.set_option("base-case", false);
  opts.set_option("forward-condition", false);
  opts.set_option("inductive-step", true);

  bmct bmc(goto_functions, opts, context, ui_message_handler);
  set_verbosity_msg(bmc);

  bmc.options.set_option("unwind", i2string(k_step));

  try {
    if(!do_bmc(bmc))
    {
      std::cout << "\nSolution found by the inductive step "
        << "(k = " << k_step << ")\n";
      return false;
    }
  }
  catch(...)
  {
    // If there is a dynamic allocation during goto symex, an
    // exception will be thrown and the inductive step is disabled
  }

  return true;
}

bool cbmc_parseoptionst::set_claims(goto_functionst &goto_functions)
{
  try
  {
    if(cmdline.isset("claim"))
      ::set_claims(goto_functions, cmdline.get_values("claim"));
  }

  catch(const char *e)
  {
    error(e);
    return true;
  }

  catch(const std::string &e)
  {
    error(e);
    return true;
  }

  catch(int)
  {
    return true;
  }

  return false;
}

bool cbmc_parseoptionst::get_goto_program(
  optionst &options,
  goto_functionst &goto_functions)
{
  fine_timet parse_start = current_time();
  try
  {
    if(cmdline.args.size()==0)
    {
      error("Please provide a program to verify");
      return true;
    }

    // If the user is providing the GOTO functions, we don't need to parse
    if(cmdline.isset("binary"))
    {
      status("Reading GOTO program from file");

      if(read_goto_binary(goto_functions))
        return true;
    }
    else
    {
      // Parsing
      if(parse()) return true;
      if(cmdline.isset("parse-tree-too") || cmdline.isset("parse-tree-only"))
      {
        assert(language_files.filemap.size());
        languaget &language = *language_files.filemap.begin()->second.language;
        language.show_parse(std::cout);

        if(cmdline.isset("parse-tree-only")) return true;
      }

      // Typecheking (old frontend) or adjust (clang frontend)
      if(typecheck()) return true;
      if(final()) return true;

      // we no longer need any parse trees or language files
      clear_parse();

      if(cmdline.isset("symbol-table-too") || cmdline.isset("symbol-table-only"))
      {
        show_symbol_table();
        if(cmdline.isset("symbol-table-only")) return true;
      }

      status("Generating GOTO Program");

      // Ahem
      migrate_namespace_lookup = new namespacet(context);

      goto_convert(
        context, options, goto_functions,
        ui_message_handler);
    }

    fine_timet parse_stop = current_time();
    std::ostringstream str;
    str << "GOTO program creation time: ";
    output_time(parse_stop - parse_start, str);
    str << "s";
    status(str.str());

    fine_timet process_start = current_time();
    if(process_goto_program(options, goto_functions))
      return true;
    fine_timet process_stop = current_time();
    std::ostringstream str2;
    str2 << "GOTO program processing time: ";
    output_time(process_stop - process_start, str2);
    str2 << "s";
    status(str2.str());
  }

  catch(const char *e)
  {
    error(e);
    return true;
  }

  catch(const std::string &e)
  {
    error(e);
    return true;
  }

  catch(std::bad_alloc&)
  {
    std::cout << "Out of memory" << std::endl;
    return true;
  }

  return false;
}

void cbmc_parseoptionst::preprocessing()
{
  try
  {
    if(cmdline.args.size()!=1)
    {
      error("Please provide one program to preprocess");
      return;
    }

    std::string filename=cmdline.args[0];

    // To test that the file exists,
    std::ifstream infile(filename.c_str());
    if(!infile)
    {
      error("failed to open input file");
      return;
    }

    if (c_preprocess(filename, std::cout, false, *get_message_handler()))
      error("PREPROCESSING ERROR");
  }

  catch(const char *e)
  {
    error(e);
  }

  catch(const std::string &e)
  {
    error(e);
  }

  catch(std::bad_alloc&)
  {
    std::cout << "Out of memory" << std::endl;
  }
}

void cbmc_parseoptionst::add_property_monitors(goto_functionst &goto_functions, namespacet &ns __attribute__((unused)))
{
  std::map<std::string, std::string> strings;

  context.foreach_operand(
    [this, &strings] (const symbolt& s)
    {
      if (s.name.as_string().find("__ESBMC_property_") != std::string::npos)
      {
        // Munge back into the shape of an actual string
        std::string str = "";
        forall_operands(iter2, s.value) {
          char c = (char)strtol(iter2->value().as_string().c_str(), NULL, 2);
          if (c != 0)
            str += c;
          else
            break;
        }

        strings[s.name.as_string()] = str;
      }
    }
  );

  std::map<std::string, std::pair<std::set<std::string>, expr2tc> > monitors;
  std::map<std::string, std::string>::const_iterator str_it;
  for (str_it = strings.begin(); str_it != strings.end(); str_it++) {
    if (str_it->first.find("$type") == std::string::npos) {
      std::set<std::string> used_syms;
      expr2tc main_expr;
      std::string prop_name = str_it->first.substr(20, std::string::npos);
      main_expr = calculate_a_property_monitor(prop_name, strings, used_syms);
      monitors[prop_name] = std::pair<std::set<std::string>, expr2tc>
                                      (used_syms, main_expr);
    }
  }

  if (monitors.size() == 0)
    return;

  Forall_goto_functions(f_it, goto_functions) {
    goto_functiont &func = f_it->second;
    goto_programt &prog = func.body;
    Forall_goto_program_instructions(p_it, prog) {
      add_monitor_exprs(p_it, prog.instructions, monitors);
    }
  }

  // Find main function; find first function call; insert updates to each
  // property expression. This makes sure that there isn't inconsistent
  // initialization of each monitor boolean.
  goto_functionst::function_mapt::iterator f_it = goto_functions.function_map.find("main");
  assert(f_it != goto_functions.function_map.end());
  Forall_goto_program_instructions(p_it, f_it->second.body) {
    if (p_it->type == FUNCTION_CALL) {
      const code_function_call2t &func_call =
        to_code_function_call2t(p_it->code);
      if (is_symbol2t(func_call.function) &&
          to_symbol2t(func_call.function).thename == "c::main")
        continue;

      // Insert initializers for each monitor expr.
      std::map<std::string, std::pair<std::set<std::string>, expr2tc> >
        ::const_iterator it;
      for (it = monitors.begin(); it != monitors.end(); it++) {
        goto_programt::instructiont new_insn;
        new_insn.type = ASSIGN;
        std::string prop_name = "c::" + it->first + "_status";
        typecast2tc cast(get_int_type(32), it->second.second);
        code_assign2tc assign(symbol2tc(get_int_type(32), prop_name), cast);
        new_insn.code = assign;
        new_insn.function = p_it->function;

        // new_insn location field not set - I believe it gets numbered later.
        f_it->second.body.instructions.insert(p_it, new_insn);
      }

      break;
    }
  }

  return;
}

static void replace_symbol_names(expr2tc &e, std::string prefix, std::map<std::string, std::string> &strings, std::set<std::string> &used_syms)
{

  if (is_symbol2t(e)) {
    symbol2t &thesym = to_symbol2t(e);
    std::string sym = thesym.get_symbol_name();

    used_syms.insert(sym);
  } else {
    e.get()->Foreach_operand([&prefix, &strings, &used_syms] (expr2tc &e)
      {
        if (!is_nil_expr(e))
          replace_symbol_names(e, prefix, strings, used_syms);
      }
    );
  }

  return;
}

expr2tc cbmc_parseoptionst::calculate_a_property_monitor(std::string name, std::map<std::string, std::string> &strings, std::set<std::string> &used_syms)
{
  exprt main_expr;
  std::map<std::string, std::string>::const_iterator it;

  namespacet ns(context);
  languagest languages(ns, MODE_C);

  std::string expr_str = strings["c::__ESBMC_property_" + name];
  std::string dummy_str = "";

  languages.to_expr(expr_str, dummy_str, main_expr, ui_message_handler);

  expr2tc new_main_expr;
  migrate_expr(main_expr, new_main_expr);
  replace_symbol_names(new_main_expr, name, strings, used_syms);

  return new_main_expr;
}

void cbmc_parseoptionst::add_monitor_exprs(goto_programt::targett insn, goto_programt::instructionst &insn_list, std::map<std::string, std::pair<std::set<std::string>, expr2tc> >monitors)
{

  // We've been handed an instruction, look for assignments to the
  // symbol we're looking for. When we find one, append a goto instruction that
  // re-evaluates a proposition expression. Because there can be more than one,
  // we put re-evaluations in atomic blocks.

  if (!insn->is_assign())
    return;

  code_assign2t &assign = to_code_assign2t(insn->code);

  // Don't allow propositions about things like the contents of an array and
  // suchlike.
  if (!is_symbol2t(assign.target))
    return;

  symbol2t &sym = to_symbol2t(assign.target);

  // Is this actually an assignment that we're interested in?
  std::map<std::string, std::pair<std::set<std::string>, expr2tc> >::const_iterator it;
  std::string sym_name = sym.get_symbol_name();
  std::set<std::pair<std::string, expr2tc> > triggered;
  for (it = monitors.begin(); it != monitors.end(); it++) {
    if (it->second.first.find(sym_name) == it->second.first.end())
      continue;

    triggered.insert(std::pair<std::string, expr2tc>(it->first, it->second.second));
  }

  if (triggered.empty())
    return;

  goto_programt::instructiont new_insn;

  new_insn.type = ATOMIC_BEGIN;
  new_insn.function = insn->function;
  insn_list.insert(insn, new_insn);

  insn++;

  new_insn.type = ASSIGN;
  std::set<std::pair<std::string, expr2tc> >::const_iterator trig_it;
  for (trig_it = triggered.begin(); trig_it != triggered.end(); trig_it++) {
    std::string prop_name = "c::" + trig_it->first + "_status";
    typecast2tc hack_cast(get_int_type(32), trig_it->second);
    symbol2tc newsym(get_int_type(32), prop_name);
    new_insn.code = code_assign2tc(newsym, hack_cast);
    new_insn.function = insn->function;

    insn_list.insert(insn, new_insn);
  }

  new_insn.type = FUNCTION_CALL;
  symbol2tc func_sym(get_empty_type(), "c::__ESBMC_switch_to_monitor");
  std::vector<expr2tc> args;
  new_insn.code = code_function_call2tc(expr2tc(), func_sym, args);
  new_insn.function = insn->function;
  insn_list.insert(insn, new_insn);

  new_insn.type = ATOMIC_END;
  new_insn.function = insn->function;
  insn_list.insert(insn, new_insn);

  return;
}

static unsigned int calc_globals_used(const namespacet &ns, const expr2tc &expr)
{

  if (is_nil_expr(expr))
    return 0;

  if (!is_symbol2t(expr)) {
    unsigned int globals = 0;

    expr->foreach_operand([&globals, &ns] (const expr2tc &e) {
      globals += calc_globals_used(ns, e);
      }
    );

    return globals;
  }

  std::string identifier = to_symbol2t(expr).get_symbol_name();
  const symbolt &sym = ns.lookup(identifier);

  if (identifier == "c::__ESBMC_alloc" || identifier == "c::__ESBMC_alloc_size")
    return 0;

  if (sym.static_lifetime || sym.type.is_dynamic_set())
    return 1;

  return 0;
}

void cbmc_parseoptionst::print_ileave_points(namespacet &ns,
                             goto_functionst &goto_functions)
{
  bool print_insn;

  forall_goto_functions(fit, goto_functions) {
    forall_goto_program_instructions(pit, fit->second.body) {
      print_insn = false;
      switch (pit->type) {
        case GOTO:
        case ASSUME:
        case ASSERT:
          if (calc_globals_used(ns, pit->guard) > 0)
            print_insn = true;
          break;
        case ASSIGN:
          if (calc_globals_used(ns, pit->code) > 0)
            print_insn = true;
          break;
        case FUNCTION_CALL:
          {
            code_function_call2t deref_code =
              to_code_function_call2t(pit->code);

            if (is_symbol2t(deref_code.function) &&
                to_symbol2t(deref_code.function).get_symbol_name()
                            == "c::__ESBMC_yield")
              print_insn = true;
          }
          break;
        default:
          break;
      }

      if (print_insn)
        pit->output_instruction(ns, pit->function, std::cout);
    }
  }

  return;
}

bool cbmc_parseoptionst::read_goto_binary(
  goto_functionst &goto_functions)
{
  std::ifstream in(cmdline.getval("binary"), std::ios::binary);

  if(!in)
  {
    error(
      std::string("Failed to open `")+
      cmdline.getval("binary")+
      "'");
    return true;
  }

  ::read_goto_binary(
    in, context, goto_functions, *get_message_handler());

  return false;
}

bool cbmc_parseoptionst::process_goto_program(
  optionst &options,
  goto_functionst &goto_functions)
{
  try
  {
    namespacet ns(context);

    // do partial inlining
    if (!cmdline.isset("no-inlining"))
    {
      if(cmdline.isset("full-inlining"))
        goto_inline(goto_functions, options, ns, ui_message_handler);
      else
        goto_partial_inline(goto_functions, options, ns, ui_message_handler);
    }

    if(cmdline.isset("inductive-step")
       || cmdline.isset("k-induction")
       || cmdline.isset("k-induction-parallel"))
    {
      // If the inductive step was disabled during the conversion,
      // there is no point spending time trying to convert it,
      // so just give up and return
      if(options.get_bool_option("disable-inductive-step"))
        return false;

      goto_k_induction(
        goto_functions,
        context,
        options,
        ui_message_handler);

      // Warn the user if the forward condition was disabled
      if(options.get_bool_option("disable-forward-condition"))
      {
        std::cout << "**** WARNING: this program contains infinite loops, "
            << "so we are not applying the forward condition!"
            << std::endl;
      }
    }

    goto_check(ns, options, goto_functions);

    // show it?
    if(cmdline.isset("show-goto-value-sets"))
    {
      value_set_analysist value_set_analysis(ns);
      value_set_analysis(goto_functions);
      show_value_sets(get_ui(), goto_functions, value_set_analysis);
      return true;
    }

#if 0
    // This disabled code used to run the pointer static analysis and produce
    // pointer assertions appropriately. Disabled now that assertions are all
    // performed at symex time.
    status("Pointer Analysis");

    status("Adding Pointer Checks");

    // add pointer checks
    pointer_checks(
      goto_functions, ns, context, options, value_set_analysis);
#endif

    // add re-evaluations of monitored properties
    add_property_monitors(goto_functions, ns);

    // remove skips
    remove_skip(goto_functions);

    // remove unreachable code
    Forall_goto_functions(f_it, goto_functions)
      remove_unreachable(f_it->second.body);

    // remove skips
    remove_skip(goto_functions);

    // recalculate numbers, etc.
    goto_functions.update();

    // add loop ids
    goto_functions.compute_loop_numbers();

    if(cmdline.isset("data-races-check"))
    {
      status("Adding Data Race Checks");

      value_set_analysist value_set_analysis(ns);
      value_set_analysis(goto_functions);

      add_race_assertions(
        value_set_analysis,
        context,
        goto_functions);

      value_set_analysis.
        update(goto_functions);
    }

    if(cmdline.isset("unroll-loops"))
    {
      if(!atol(options.get_option("unwind").c_str()))
      {
        std::cerr << "Max unwind must be set to unroll loops" << std::endl;
        abort();
      }

      goto_unwind(
        context,
        goto_functions,
        atol(options.get_option("unwind").c_str()),
        ui_message_handler);
    }

    // show it?
    if(cmdline.isset("show-loops"))
    {
      show_loop_numbers(get_ui(), goto_functions);
      return true;
    }

    if (cmdline.isset("show-ileave-points"))
    {
      print_ileave_points(ns, goto_functions);
      return true;
    }

    // show it?
    if(cmdline.isset("goto-functions-too") || cmdline.isset("goto-functions-only"))
    {
      goto_functions.output(ns, std::cout);
      if(cmdline.isset("goto-functions-only")) return true;
    }
  }

  catch(const char *e)
  {
    error(e);
    return true;
  }

  catch(const std::string &e)
  {
    error(e);
    return true;
  }

  catch(std::bad_alloc&)
  {
    std::cout << "Out of memory" << std::endl;
    return true;
  }

  return false;
}

int cbmc_parseoptionst::do_bmc(bmct &bmc1)
{
  bmc1.set_ui(get_ui());

  // do actual BMC

  status("Starting Bounded Model Checking");

  bool res = bmc1.run();

#ifdef HAVE_SENDFILE_ESBMC
  if (bmc1.options.get_bool_option("memstats")) {
    int fd = open("/proc/self/status", O_RDONLY);
    sendfile(2, fd, NULL, 100000);
    close(fd);
  }
#endif

  return res;
}

void cbmc_parseoptionst::help()
{
  std::cout <<
    "\n"
    "* * *           ESBMC " ESBMC_VERSION "          * * *\n"
    "\n"
    "Usage:                       Purpose:\n"
    "\n"
    " esbmc [-?] [-h] [--help]      show help\n"
    " esbmc file.c ...              source file names\n"

    "\nAdditonal options:\n"

    "\nOutput options\n"
    " --parse-tree-only            only show parse tree\n"
    " --parse-tree-too             show parse tree and verify\n"
    " --symbol-table-only          only show symbol table\n"
    " --symbol-table-too           show symbol table and verify\n"
    " --goto-functions-only        only show goto program\n"
    " --goto-functions-too         show goto program and verify\n"
    " --program-only               only show program expression\n"
    " --program-too                show program expression and verify\n"
    " --show-guards                print SSA's guards, if any\n"
    " --simple-ssa-printing        do not print the SSA's original location\n"
    " --smt-formula-only           only show SMT formula (not supported by all solvers)\n"
    " --smt-formula-too            show SMT formula (not supported by all solvers) and verify\n"
    " --show-smt-model             show SMT model (not supported by all solvers), if the formula is SAT\n"

    "\nTrace options\n"
    " --quiet                      do not print unwinding information during symbolic execution\n"
    " --symex-trace                print instructions during symbolic execution\n"
    " --symex-ssa-trace            print generated SSA during symbolic execution\n"
    " --ssa-trace                  print SSA during SMT encoding\n"
    " --show-goto-value-sets       show value-set analysis for the goto functions\n"
    " --show-symex-value-sets      show value-set analysis during symbolic execution\n"

    "\nFront-end options\n"
    " -I path                      set include path\n"
    " -D macro                     define preprocessor macro\n"
    " --preprocess                 stop after preprocessing\n"
    " --no-inlining                disable inlining function calls\n"
    " --full-inlining              perform full inlining of function calls\n"
    " --all-claims                 keep all claims\n"
    " --show-loops                 show the loops in the program\n"
    " --show-claims                only show claims\n"
    " --show-vcc                   show the verification conditions\n"
    " --document-subgoals          generate subgoals documentation\n"
    " --no-arch                    don't set up an architecture\n"
    " --no-library                 disable built-in abstract C library\n"
    " --binary                     read goto program instead of source code\n"
    " --little-endian              allow little-endian word-byte conversions\n"
    " --big-endian                 allow big-endian word-byte conversions\n"
    " --16, --32, --64             set width of machine word (default is 64)\n"
    " --unsigned-char              make \"char\" unsigned by default\n"
    " --version                    show current ESBMC version and exit\n"
    " --witness-output filename    generate a verification result witness in GraphML format\n"
    " --witness-detailed           generate line offset when generating a witness (linux only)\n"
    " --clang-frontend             parse source files using clang (experimental)\n"
    " --result-only                do not print the counter-example\n"
    #ifdef _WIN32
    " --i386-macos                 set MACOS/I386 architecture\n"
    " --ppc-macos                  set PPC/I386 architecture\n"
    " --i386-linux                 set Linux/I386 architecture\n"
    " --i386-win32                 set Windows/I386 architecture (default)\n"
    #elif __APPLE__
    " --i386-macos                 set MACOS/I386 architecture (default)\n"
    " --ppc-macos                  set PPC/I386 architecture\n"
    " --i386-linux                 set Linux/I386 architecture\n"
    " --i386-win32                 set Windows/I386 architecture\n"
    #else
    " --i386-macos                 set MACOS/I386 architecture\n"
    " --ppc-macos                  set PPC/I386 architecture\n"
    " --i386-linux                 set Linux/I386 architecture (default)\n"
    " --i386-win32                 set Windows/I386 architecture\n"
    #endif

    "\nBMC options\n"
    " --function name              set main function name\n"
    " --claim nr                   only check specific claim\n"
    " --depth nr                   limit search depth\n"
    " --unwind nr                  unwind nr times\n"
    " --unwindset nr               unwind given loop nr times\n"
    " --no-unwinding-assertions    do not generate unwinding assertions\n"
    " --partial-loops              permit paths with partial loops\n"
    " --unroll-loops               unwind all loops by the value defined by the --unwind option\n"
    " --no-slice                   do not remove unused equations\n"
    " --extended-try-analysis      check all the try block, even when an exception is thrown\n"

    "\nIncremental BMC\n"
    " --falsification              incremental loop unwinding for bug searching\n"
    " --incremental-bmc            incremental loop unwinding verification\n"
    " --k-step nr                  set k increment (default is 1)\n"
    " --max-k-step nr              set max number of iteration (default is 50)\n"
    " --unlimited-k-steps          set max number of iteration to UINT_MAX\n"

    "\nSolver configuration\n"
    " --list-solvers               list available solvers and exit\n"
    " --boolector                  use Boolector (default)\n"
    " --z3                         use Z3\n"
    " --mathsat                    use MathSAT\n"
    " --cvc                        use CVC4\n"
    " --yices                      use Yices\n"
    " --bv                         use solver with bit-vector arithmetic\n"
    " --ir                         use solver with integer/real arithmetic\n"
    " --smtlib                     use SMT lib format\n"
    " --smtlib-solver-prog         SMT lib program name\n"
    " --output <filename>          output VCCs in SMT lib format to given file\n"
    " --fixedbv                    encode floating-point as fixed bitvectors (default)\n"
    " --floatbv                    encode floating-point using the SMT floating-point theory\n"

    "\nIncremental SMT solving\n"
    " --smt-during-symex           enable incremental SMT solving (experimental)\n"
    " --smt-thread-guard           call the solver during thread exploration (experimental)\n"
    " --smt-symex-guard            call the solver during symbolic execution (experimental)\n"

    "\nProperty checking\n"
    " --no-assertions              ignore assertions\n"
    " --no-bounds-check            do not do array bounds check\n"
    " --no-div-by-zero-check       do not do division by zero check\n"
    " --no-pointer-check           do not do pointer check\n"
    " --no-align-check             do not check pointer alignment\n"
    " --memory-leak-check          enable memory leak check check\n"
    " --nan-check                  check floating-point for NaN\n"
    " --overflow-check             enable arithmetic over- and underflow check\n"
    " --deadlock-check             enable global and local deadlock check with mutex\n"
    " --data-races-check           enable data races check\n"
    " --lock-order-check           enable for lock acquisition ordering check\n"
    " --atomicity-check            enable atomicity check at visible assignments\n"
    " --error-label label          check if label is unreachable\n"
    " --force-malloc-success       do not check for malloc/new failure\n"

    "\nK-induction\n"
    " --base-case                  check the base case\n"
    " --forward-condition          check the forward condition\n"
    " --inductive-step             check the inductive step\n"
    " --k-induction                prove by k-induction \n"
    " --k-induction-parallel       prove by k-induction, running each step on a separate\n"
    "                              process\n"
    " --constrain-all-states       remove all redundant states in the inductive step\n"
    " --k-step nr                  set k increment (default is 1)\n"
    " --max-k-step nr              set max number of iteration (default is 50)\n"
    " --unlimited-k-steps          set max number of iteration to UINT_MAX\n"
    " --show-counter-example       print the counter-example produced by the inductive step\n"

    "\nScheduling approaches\n"
    " --schedule                   use schedule recording approach \n"
    " --round-robin                use the round robin scheduling approach\n"
    " --time-slice nr              set the time slice of the round robin algorithm\n"
    "                              (default is 1) \n"

    "\nConcurrency checking\n"
    " --context-bound nr           limit number of context switches for each thread \n"
    " --state-hashing              enable state-hashing, prunes duplicate states\n"
    " --control-flow-test          enable context switch before control flow tests\n"
    " --no-por                     do not do partial order reduction\n"
    " --all-runs                   check all interleavings, even if a bug was already found\n"

    "\nMiscellaneous options\n"
    " --memlimit                   configure memory limit, of form \"100m\" or \"2g\"\n"
    " --timeout                    configure time limit, integer followed by {s,m,h}\n"
    " --memstats                   print memory usage statistics\n"
    " --no-simplify                do not simplify any expression\n"
    " --enable-core-dump           do not disable core dump output\n"
    "\n";
}
