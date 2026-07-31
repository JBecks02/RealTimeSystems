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

<img width="484" height="514" alt="Screenshot 2026-07-31 at 8 10 10 AM" src="https://github.com/user-attachments/assets/ee847481-6003-4a16-b58e-396d18915fbc" />

BELOW IS THE APPLICATION SYSTEM ARCHITECTURE DIAGRAM

<img width="522" height="672" alt="Untitled Diagram drawio (23)" src="https://github.com/user-attachments/assets/4db10920-da32-4153-8c4c-81c1a306cd24" />


TASK TABLE


