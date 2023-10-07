// Write to serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

#include "flags.h"

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256

volatile int STOP = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;
char data[1000];
int data_size = 0;

int state = UA_START;

int fd;
struct termios oldtio;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}

int llopen(int porta, int individual) {
    // Program usage: Uses either COM1 or COM2

    char *v[3];
    char *serialPortName;

    int length = snprintf(NULL, 0, "%d", porta);
    char *door = malloc(length + 1);
    snprintf(door, length + 1, "%d", porta);

    v[0] = "/dev/ttyS";
    v[1] = door;
    v[2] = "\0";

    serialPortName = malloc(strlen(v[0]) + strlen(v[1]) + 2);

    sprintf(serialPortName, "%s%s", v[0], v[1]);

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    free(door);
    free(serialPortName);

    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    if (individual) {

        // Create string to send
        unsigned char buf[BUF_SIZE] = {0};

        buf[0] = FLAG;
        buf[1] = SND_A;
        buf[2] = SET;
        buf[3] = buf[1]^buf[2];
        buf[4] = FLAG;

        // Wait until all bytes have been written to the serial port
        sleep(1);

        unsigned char rbuf[1] = {0};

        int rcv_a, rcv_c;

        (void)signal(SIGALRM, alarmHandler);

        while (state != UA_STOP && alarmCount < 4) {

            if (alarmEnabled == FALSE)
            {
                alarm(3); // Set alarm to be triggered in 3s
                alarmEnabled = TRUE;

                int bytes = write(fd, buf, BUF_SIZE);
                printf("%d bytes written\n", bytes);
            }

            int bytes = read(fd, rbuf, 1);

            if (bytes > 0) {

                switch (state)
                {
                case UA_START:
                    if (rbuf[0] == FLAG) 
                        state = UA_FLAG_RCV; 
                    break;

                case UA_FLAG_RCV:
                    if (rbuf[0] == RCV_A) {
                        state = UA_A_RCV;
                        rcv_a = rbuf[0];
                    }
                    else if (rbuf[0] == FLAG)
                        break;
                    else 
                        state = UA_START;
                    break;

                case UA_A_RCV:
                    if (rbuf [0] == UA) {
                        state = UA_C_RCV;
                        rcv_c = rbuf[0];
                    }
                    else if (rbuf[0] == FLAG)
                        state = UA_FLAG_RCV;
                    else
                        state = UA_START;
                    break;
                
                case UA_C_RCV:
                    if (rbuf[0] == rcv_a^rcv_c)
                        state = UA_BCC_OK;
                    else if (rbuf[0] == FLAG)
                        state = UA_FLAG_RCV;
                    else 
                        state = UA_START;
                    break;
                
                case UA_BCC_OK:
                    if (rbuf[0] == FLAG)
                        state = UA_STOP;
                    else 
                        state = UA_START;
                    break;
                
                default:
                    break;
                }

            }

        }

        if (alarmCount == 4) 
            printf("That bitch didnt receive my set up.\n");

        else 
            printf("Received UA information.\n");

    }
    else {
        unsigned char newbuf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
	unsigned char buf[1] = {0};
	
	int snd_a;
	int snd_c;
	
    while (state != SET_STOP)
    {
        // Returns after 5 chars have been input
        int bytes = read(fd, buf, 1);
        //buf[bytes] = '\0'; // Set end of string to '\0', so we can printf
        if(bytes >0){
			switch(state){
				case SET_START:
					if (buf[0] == FLAG)
						state = SET_FLAG_RCV;
					
					break;
					
				case SET_FLAG_RCV:
					if(buf[0] == SND_A){
						state = SET_A_RCV;
						snd_a = buf[0];
					} else if (buf[0] == FLAG){
						break;
					} else {
						state = SET_START;
					}
					break;
						
				case SET_A_RCV:
					if(buf[0] == SET){
						snd_c = buf[0];
						state = SET_C_RCV;
					} else if (buf[0] == FLAG){
						state = SET_FLAG_RCV;
					} else {
						state = SET_START;
					}			
					break;
							
				case SET_C_RCV:
					if(buf[0] == snd_a^snd_c){
						state = SET_BCC_OK;
					} else if (buf[0] == FLAG){
						state = SET_FLAG_RCV;
					} else {
						state = SET_START;
					}			
					break;
					
				case SET_BCC_OK:
					if(buf[0] == FLAG){
						state = SET_STOP;
					} else {
						state = SET_START;
					}		
					break;
				
				default:
					
					break;
			}
		}		

    }
    
    printf("Received SET information.\n");
    
	
	newbuf[0]=FLAG;
	newbuf[1]=RCV_A;
	newbuf[2]=UA;
	newbuf[3]=newbuf[1]^newbuf[2];
	newbuf[4]=FLAG;
	
	int bytes = write(fd, newbuf, BUF_SIZE);
    printf("%d bytes written\n", bytes);
    }

}

int llread(int fd, char * buffer) {
    	
	unsigned char newbuf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
	unsigned char buf[1] = {0};
	
	int snd_a;
	int snd_c;
	int temp;
    int bcc2;
	int loop = 1;

    while (loop)
    {
        // Returns after 5 chars have been input
        int bytes = read(fd, buf, 1);
        //buf[bytes] = '\0'; // Set end of string to '\0', so we can printf
        if(bytes >0){
			switch(state){
				case SET_START:
					if (buf[0] == FLAG)
						state = SET_FLAG_RCV;
					
					break;
					
				case SET_FLAG_RCV:
					if(buf[0] == SND_A){
						state = SET_A_RCV;
						snd_a = buf[0];
					} else if (buf[0] == FLAG){
						break;
					} else {
						state = SET_START;
					}
					break;
						
				case SET_A_RCV:

					if(buf[0] == IN0 || buf[0] == IN1){
						snd_c = buf[0];
						state = SET_C_RCV;
					} else if (buf[0] == FLAG){
						state = SET_FLAG_RCV;
					} else {
						state = SET_START;
					}			
					break;
							
				case SET_C_RCV:
					if(buf[0] == snd_a^snd_c){
						state = SET_BCC_OK;
					} else if (buf[0] == FLAG){
						state = SET_FLAG_RCV;
					} else {
						state = SET_START;
					}			
					break;
					
				case SET_BCC_OK:
					if(buf[0] == FLAG){
						state = SET_STOP;
					} else {
						state = INFO;
					}		
					break;
                    
                case INFO:
                    if(buf[0] == FLAG){
                        state = SET_STOP;
                    } else {
                        state = INFO;
                        data[data_size] = buf[0];
                        data_size++;
                    }
                    break;

                case SET_STOP:
                    data[data_size]= "\0";
                    bcc2 = data[0];
                    for (int j = 1; j < sizeof(data)-2; j++){
                        bcc2 = bcc2 ^ data[j]; 
                    }
                    if (bcc2 == data[sizeof(data)-2]){
                        if (snd_c == IN0){
                            newbuf[2] = RR1;
                        } else if (snd_c == IN1){
                            newbuf[2] = RR0;
                        }
                    } else {
                        if (snd_c == IN0) {
                            newbuf[2] = REJ0;
                        } else if (snd_c == IN1){
                            newbuf[2] = REJ1;
                        }
                    }
                    buffer = data;
                    memset(data,0,data_size+1);
                    data_size = 0;
                    loop = 0;
                    break;

			}
		}
    }
    
    printf("Received information.\n");
    
	newbuf[0]=FLAG;
	newbuf[1]=RCV_A;
	newbuf[3]=newbuf[1]^newbuf[2];
	newbuf[4]=FLAG;
	
	int bytes = write(fd, newbuf, BUF_SIZE);
}

int llwrite(char *information) {

    int packets_count = sizeof(information)/(BUF_SIZE - 6);

    for (int iteration = 0; iteration < packets_count; iteration++) {

        // Create string to send
    unsigned char buf[BUF_SIZE] = {0};

    buf[0] = FLAG;
    buf[1] = SND_A;
    buf[2] = SET;
    buf[3] = buf[1]^buf[2];
    buf[4] = FLAG;

    // Wait until all bytes have been written to the serial port
    sleep(1);

    unsigned char rbuf[1] = {0};

    int rcv_a, rcv_c;

    (void)signal(SIGALRM, alarmHandler);

    while (state != UA_STOP && alarmCount < 4) {

        if (alarmEnabled == FALSE)
        {
            alarm(3); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;

            int bytes = write(fd, buf, BUF_SIZE);
            printf("%d bytes written\n", bytes);
        }

        int bytes = read(fd, rbuf, 1);

        if (bytes > 0) {

            switch (state)
            {
            case UA_START:
                if (rbuf[0] == FLAG) 
                    state = UA_FLAG_RCV; 
                break;

            case UA_FLAG_RCV:
                if (rbuf[0] == RCV_A) {
                    state = UA_A_RCV;
                    rcv_a = rbuf[0];
                }
                else if (rbuf[0] == FLAG)
                    break;
                else 
                    state = UA_START;
                break;

            case UA_A_RCV:
                if (rbuf [0] == UA) {
                    state = UA_C_RCV;
                    rcv_c = rbuf[0];
                }
                else if (rbuf[0] == FLAG)
                    state = UA_FLAG_RCV;
                else
                    state = UA_START;
                break;
            
            case UA_C_RCV:
                if (rbuf[0] == rcv_a^rcv_c)
                    state = UA_BCC_OK;
                else if (rbuf[0] == FLAG)
                    state = UA_FLAG_RCV;
                else 
                    state = UA_START;
                break;
            
            case UA_BCC_OK:
                if (rbuf[0] == FLAG)
                    state = UA_STOP;
                else 
                    state = UA_START;
                break;
            
            default:
                break;
            }

        }

    }

    if (alarmCount == 4) 
        printf("That bitch didnt receive my set up.\n");

    else 
        printf("Received UA information.\n");

    }
    
    return 0;

}

int llclose() {

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }
    close(fd);
    return 0;

}

int main(int argc, char *argv[])
{
    // Program usage: Uses either COM1 or COM2

    if (argc < 3)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPortNumber> <Transmitter = 1 or Receiver = 0>\n"
               "Example: %s 1 1 (Port number 1 and Transmitter\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    int num;
    int individual;
    sscanf(argv[1], "%d", &num);
    sscanf(argv[2], "%d", &individual);

    llopen(num, individual);
    printf("%d",fd);
    llclose();

}
