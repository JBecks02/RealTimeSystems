##README

# RealTimeSystems
Final Project for real-time systems, using an ESP-32 dual-core to mimic an avionics control system.

YouTube Demo link: https://youtu.be/WJ2yD7cto4M
Wowki Link: https://wokwi.com/projects/469984409971783681


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
'''
| Task                                | Priority |   Core  | Period / Trigger          | Synchronization Used                       | Purpose                                                                           |
| ----------------------------------- | :------: | :-----: | ------------------------- | ------------------------------------------ | --------------------------------------------------------------------------------- |
| **Responder Task**                  |  **12**  | APP_CPU | Event-driven (button ISR) | **Binary Semaphore**                       | Waits for the ISR to signal a radar/collision event and processes it immediately. |
| **Pool Consumer 1**                 |   **5**  | APP_CPU | Continuous                | **Counting Semaphore**                     | Acquires one of three available communication channels before transmitting.       |
| **Pool Consumer 2**                 |   **5**  | APP_CPU | Continuous                | **Counting Semaphore**                     | Same as Consumer 1.                                                               |
| **Pool Consumer 3**                 |   **5**  | APP_CPU | Continuous                | **Counting Semaphore**                     | Same as Consumer 1.                                                               |
| **Pool Consumer 4**                 |   **5**  | APP_CPU | Continuous                | **Counting Semaphore**                     | Demonstrates resource contention because four tasks share only three channels.    |
| **Flight Data Writer 1**            |   **8**  | APP_CPU | ~223 ms                   | **Mutex** (or none during induced failure) | Updates the shared flight-state counter safely.                                   |
| **Flight Data Writer 2**            |   **8**  | APP_CPU | ~296 ms                   | **Mutex** (or none during induced failure) | Concurrently updates the same shared flight-state counter.                        |
| **Priority Inversion – High (H)**   |  **15**  | APP_CPU | One-shot after 50 ms      | **Mutex / Binary Semaphore**               | Attempts to acquire the shared lock and measures wait time.                       |
| **Priority Inversion – Medium (M)** |  **10**  | APP_CPU | One-shot after 100 ms     | None                                       | CPU-intensive interference task demonstrating priority inversion.                 |
| **Priority Inversion – Low (L)**    |   **5**  | APP_CPU | Starts immediately        | **Mutex / Binary Semaphore**               | Holds the shared lock while executing a long CPU-bound critical section.          |

ADDED FOR CAPSTONE
| Task           | Priority |   Core  |    Period   | Purpose                                                                                                       |
| -------------- | :------: | :-----: | :---------: | ------------------------------------------------------------------------------------------------------------- |
| Dashboard Task |   **1**  | APP_CPU | **2000 ms** | Displays resource utilization, semaphore usage, task activity, ISR counts, and priority inversion statistics. |
'''

FINAL REFLECTION:

This project was one that reinforced the topics learned throughout EEE4775, which include but are not limited to processing system architecture in an operating system governed enviornment where threads have the ability to preempt and take control of our system's resources. As mentioned in the project overview, we used semaphores, mutexes, and ISRs to achieve this functionality. The CAPSTONES added feature of graphical resource management, I think, is something any embedded engineer can take from, as understanding how and when resources would be utilized at the millisecond range is extremely hard to imagine and understand without a full picture. The effect of difficult understanding of RTOS and resource management is something I think is inherently baked into the ideas of what it means to "operate" in an embedded environment that uses FreeRTOS, and why I chose to add this functionality to APP4 to submit as my capstone project. The most valuable thing I can take away from this project is the lesson of breaking down extremely compact and dense lines of logic, to then trace back and re-assemble into a much easier-to-digest package. Anytime you are able to start from the high level, walk down to the fundamentals of what is happening in a system, and rebuild that answer, you gain so much insight into the detail of how that system works. If I were to redo this project, I would most definitely not use the system console to graphically display my data, as this is a poor way to display the important information, instead of would opt for either transmitting all the key resource data to an external device that can give real time updates for each variable as is, without needed to reload all information, or use a connectable device like a tablet screen to be able to give a cleaner display, or if i wanted to stay in the software side, a simple GUI could probably be easily built for this web-applicaiton, especailly if using another core to start up a web interface was used like in the first 3 applications. Any solution is better than a simple print to the terminal, but given the limited time and near end of the semester, with crunching for all the other classes and getting ready to graduate, I think my solution is 'ok'. There is always room to optimize, but never enough time for it.


