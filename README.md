##README

# RealTimeSystems
Final Project for real-time systems, using an ESP-32 dual-core to mimic an avionics control system.


## PROJECT OVERVIEW ##
This capstone project's main system is the code from APP4, in which we implemented synchronization techniques to achieve our deadlines. APP4 used the following,

Binary semaphore — ISR → task signaling
Counting semaphore — manage a pool of resources OR backlog of events
Mutex — protect a shared resource that two tasks both modify

Also note that we demonstrated priority inversion, which is shown in the standard APP4 output. The logic behind APP4 and the changes I made was to illustrate the effects of using an OS to achieve our deadlines, and how the behavior and overall system performance are affected when using different approaches. Understand how critical sections are handled, how context switching can affect a system's response time, and how resource allocation is shared. And what may cause race conditions or break the reliability of our system. 

##Changes and additions made to APP4 to create a capstone include a graphical interface printed in the serial console to show resource utilization. The thought and reasoning for choosing this is that dealing with and understanding how the resource allocation and partitioning under an RTOS can become extremely difficult to understand and follow when threading and context switching happen, and even though I did not implement a nice-looking GUI, I feel that a printout of all the resources showing how and when they are used is still very helpful.

BELOW IS AN EXAMPLE OF THE RESOURCE PRINT OUT

+================================================================+
|              AVIATION RESOURCE UTILIZATION DASHBOARD           |
+================================================================+
| Uptime:    12480 ms | Core: 1 | Lock: PI mutex                |
+----------------------------------------------------------------+
| RADIO CHANNEL POOL                                             |
| Busy [#############-------] 2/3 ( 66%) | Free: 1 | Peak: 3/3 |
+----------------------------------------------------------------+
| ACTIVITY DURING LAST 2000 ms                                   |
| Radio 1  [############--------]   3 acquisitions | timeouts: 0 |
| Radio 2  [####################]   5 acquisitions | timeouts: 0 |
| Radio 3  [########------------]   2 acquisitions | timeouts: 1 |
| Radio 4  [################----]   4 acquisitions | timeouts: 2 |
| Writer 1 [####################]  10 updates | stack free: 2056 |
| Writer 2 [################----]   8 updates | stack free: 2072 |
+----------------------------------------------------------------+
| SYNCHRONIZATION HEALTH                                         |
| Radar ISR events: 6 (+1) | handled: 6                          |
| Writer attempts: 105 | shared counter: 101 | lost: 4           |
| Mutex protection: OFF | race indicator: WARNING                |
+----------------------------------------------------------------+
| PRIORITY INVERSION TELEMETRY                                   |
| H wait: 123456 us | L hold: 511223 us | M run: 984331 us       |
+----------------------------------------------------------------+
| Heartbeat GPIO 2: ON | Responder stack free: 2136              |
+================================================================+

