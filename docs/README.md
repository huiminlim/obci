# OBC

This repo contains development of a simplified On-board Computer (OBC) of a CubeSat. 

This is part of a simplified software implementation of a CubeSat for FYP.

## About CubeSat Design

The OBC is at the heart of of the CubeSat and manages several subsystems. 

The overall configuration of the CubeSat is as such:

```                                           
                                                          
    CSP / I2C Bus                                         
    --------------------------------------------------    
    /          |           |             |           \    
   /           |           |             |            \   
+-----+    +------+    +------+    +----------+    +-----+
| EPS |    | ADCS |    | TT&C |    | Payload  |    | OBC |
+-----+    +------+    +------+    | Computer |    +-----+
                                   +----------+           
                                         |                
                                         | UART           
                                         |                
                                    +---------+           
                                    | Payload |           
                                    | Manager |           
                                    +---------+                
```

Each subsystem also has a CSP address for other nodes to initiate communication with itself.

|    Subsystem     | CSP Address |
|------------------|-------------|
| OBC              |           1 |
| EPS              |           2 |
| ADCS             |           3 |
| TT&C             |           5 |
| Payload Computer |           6 |

## Current Development Progress

Currently, this project is developed for the ATmega 2560 microcontroller and besides the OBC, the CubeSat design has only the EPS subsystem.

```
+---------------+                                   +---------------+
|               |                 CSP               |               |
|      OBC      |-----------------------------------|   Subsystem   |
|               |               I2C Bus             |               |
+---------------+                                   +---------------+
```

The OBC will have a CSP address of 1, while the Subsystem will have a CSP address of 8.

This may be expanded to accommodate other AVR microcontroller when more hardware subsystems are included in the project.

This is the expected output.

OBC output: 

![OBC output](Capture_obc.PNG)

Subsystem output: 

![EPS output](Capture_eps.PNG)
