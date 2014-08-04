#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "causal.h"
#include "counter.h"
#include "spinlock.h"
#include "support.h"
#include "thread_state.h"

/// Type of a thread entry function
typedef void* (*thread_fn_t)(void*);

enum {
  SampleSignal = SIGPROF, //< Signal to generate when samples are ready
  SamplePeriod = 1000000, //< Time between samples (1ms)
  SampleBatchSize = 10,   //< Samples to batch together for one processing run
  SpeedupDivisions = 20,  //< How many different speedups to try (20 = 5% increments)
  ExperimentMinTime = SamplePeriod * SampleBatchSize * 10,  //< Minimum experiment length
  ExperimentCoolOffTime = SamplePeriod * SampleBatchSize,   //< Time to wait after an experiment
  ExperimentMinCounterChange = 5, //< Minimum change in counters before experiment can end 
  ExperimentMinDelays = 5,        //< Minimum delays to insert before experiment can end
  ExperimentAbortThreshold = 100000000  //< Time to give up on inserting enough delays (100ms)
};

class profiler {
public:
  /// Start the profiler
  void startup(const std::string& outfile, causal_support::line* fixed_line, int fixed_speedup);
  
  /// Shut down the profiler
  void shutdown();
  
  /// Register a progress counter
  void register_counter(counter* c);
  
  /// Pass local delay counts and excess delay time to the child thread
  int handle_pthread_create(pthread_t*, const pthread_attr_t*, thread_fn_t, void*);
  
  /// Force threads to catch up on delays, and stop sampling before the thread exits
  void handle_pthread_exit(void*) __attribute__((noreturn));
  
  /// Skip any global delays this thread is missing
  void skip_delays();
  
  /// Catch up on delays
  void catch_up();
  
  /// Only allow one instance of the profiler, and never run the destructor
  static profiler& get_instance() {
    static char buf[sizeof(profiler)];
    static profiler* p = new(buf) profiler();
    return *p;
  }

private:
  profiler()  {
    _experiment_active.store(false);
    _delays.store(0);
    _delay_size.store(0);
    _selected_line.store(nullptr);
    _next_line.store(nullptr);
    _running.store(true);
  }
  
  // Disallow copy and assignment
  profiler(const profiler&) = delete;
  void operator=(const profiler&) = delete;
  
  void profiler_thread(spinlock& l);  //< Body of the main profiler thread
  void begin_sampling();  //< Start sampling in the current thread
  void end_sampling();    //< Stop sampling in the current thread
  void process_samples(thread_state::ref&); //< Process all available samples and insert delays
  causal_support::line* find_line(perf_event::record&); //< Map a sample to its source line
  
  static void* start_profiler_thread(void*);          //< Entry point for the profiler thread
  static void* start_thread(void* arg);               //< Entry point for wrapped threads
  static void samples_ready(int, siginfo_t*, void*);  //< Signal handler for sample processing
  static void on_error(int, siginfo_t*, void*);       //< Handle errors
  
  std::vector<counter*> _counters;  //< All the progress points
  spinlock _counters_lock;          //< Spinlock to protect the counters list
  
  std::atomic<bool> _experiment_active;   //< Is an experiment running?
  std::atomic<size_t> _delays;            //< The total number of delays inserted
  std::atomic<size_t> _delay_size;        //< The current delay size
  std::atomic<causal_support::line*> _selected_line;  //< The line to speed up
  std::atomic<causal_support::line*> _next_line;      //< The next line to speed up
  
  std::string _output_filename;       //< File for profiler output
  causal_support::line* _fixed_line;  //< The only line that should be sped up, if set
  int _fixed_delay_size = -1;         //< The only delay size that should be used, if set
  
  pthread_t _profiler_thread;         //< Handle for the profiler thread
  size_t _end_time;                   //< Time that shutdown was called
  std::atomic<bool> _running;         //< Clear to signal the profiler thread to quit
  std::atomic_flag _shutdown_run = ATOMIC_FLAG_INIT;  //< Used to ensure shutdown only runs once
};

#endif
