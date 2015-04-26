Mini-POSIX-Shell
================

Implementation:
--------------
Shell creates 3 threads:
* Input handling thread
* Execution handling thread
* Signal handling thread

Input handling thread is processing the user input which is then
used in execution handling thread (monitor is used for synchronization)
which forks the new process and then executes the user input. Input
and execution threads have all signals blocked so only signal handling
thread can receive signals delivered to the main shell process.

Mini POSIX Shell features:
--------------
* file redirection using >FILE or <FILE
* run process in background by specifying '&' character at the end of the command line

Mini POSIX Shell built-in commands:
--------------
* **jobs** - prints all background jobs
* **cd**   - change working directory
* **exit** - exits the shell
