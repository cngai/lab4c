// NAME: Christopher Ngai
// EMAIL: cngai1223@gmail.com
// ID: 404795904

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <math.h>
#include <mraa.h>
#include <mraa/aio.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>
#include <ctype.h>
#include <sys/socket.h>	//for socket(7), connect(2)
#include <sys/types.h>	//for connect(2)
#include <netinet/in.h>	//for IPv4 protocol implementation
#include <netdb.h>		//for gethostbyname(3)
#include <arpa/inet.h>	//for htons(3)
#include <poll.h>		//for poll(2)

/********************/
/* GLOBAL VARIABLES */
/********************/

//command line argument variables
int period_duration = 1;		//default is 1 second
char scale_opt = 'F';			//default is Fahrenheit
char *log_filename = NULL;
int log_arg = 0;
int log_fd;
char* id_num;
int id_length = 0;
char *hostname = NULL;
int port_num;

//temperature-related variables
const int B = 4275;				//B value of the thermistor
const int R0 = 100000;			//R0 = 100k
mraa_aio_context temp_sensor;
int input_temp = 0;

//poll-related variables
struct pollfd pollfd_array[1];	//array that holds pollfd structs

//socket-related variables
int sock_fd;						//holds socket file descriptor
struct sockaddr_in serv_address;	//holds server address
struct hostent* serv_info;			//holds attributes of server

//miscellaneous variables
int stop = 0;
int valid_command = 1;			//flag to see if command is valid or not

/********************/
/* HELPER FUNCTIONS */
/********************/

//takes input temperature and converts it to actual temperature based on F or C scale
float temp_converter(int init_temp, char scale){
	float R = 1023.0 / (init_temp - 1.0);
	R = R0 * R;
	float temp_cels = 1.0 / (log(R/R0) / B + 1/298.15) - 273.15;		//convert temperature in celsius

	//return temperature based on scale
	if (scale == 'F'){
		float temp_fahr = temp_cels * 9/5 + 32;
		return temp_fahr;
	}
	else{
		return temp_cels;
	}
}

//if OFF command, handle shutdown properly
void handle_shutdown(int fd){
	//initialize time struct
	struct tm* sd_time_values;
	time_t sd_timer;
	char sd_time_string[10];

	//get final time
	time_t sd_time_ret = time(&sd_timer);
	if (sd_time_ret == -1){
		fprintf(stderr, "Error getting shutdown time.\n");
		exit(1);
	}
	sd_time_values = localtime(&sd_timer);								//converts time in timer into local time
	strftime(sd_time_string, 10, "%H:%M:%S", sd_time_values);			//formats time and places into time_string array

	//create report for each sample
	dprintf(sock_fd, "%s SHUTDOWN\n", sd_time_string);					//write report to socket
	if (log_arg){
		dprintf(fd, "%s SHUTDOWN\n", sd_time_string);					//write report to log file
	}
}

//change scale based on input scale
void change_scale(char input_scale){
	scale_opt = input_scale;
}

//change stop based on input
void change_stop(int input){
	stop = input;				//1 means stop, 0 means start (don't stop)
}

//change period
void change_period(int new_period){
	period_duration = new_period;
}

//deal with invalid commands
void process_invalid_command(){
	dprintf(sock_fd, "%s\n", "Error: invalid command.");
	valid_command = 0;			//command is no longer valid
}

//processes commands from socket and performs them
void perform_command(char* command){
	if (strcmp(command, "SCALE=F") == 0){
		change_scale('F');
	}
	else if (strcmp(command, "SCALE=C") == 0){
		change_scale('C');
	}
	else if (strcmp(command, "STOP") == 0){
		change_stop(1);
	}
	else if (strcmp(command, "START") == 0){
		change_stop(0);
	}
	else if (strcmp(command, "OFF") == 0){
		dprintf(log_fd, "OFF\n");
		handle_shutdown(log_fd);
		exit(0);
	}
	else{
		char period_string[] = "PERIOD=";
		int p_cnt = 0;							//iterates through period_string
		int matches_period = 1;

		if (strlen(command) > strlen(period_string)){
			//check to see if command starts with "PERIOD="
			while (command[p_cnt] != '\0' && period_string[p_cnt] != '\0'){
				if (command[p_cnt] != period_string[p_cnt]){
					matches_period = 0;
				}
				p_cnt++;
			}
		}
		else{
			matches_period = 0;
		}

		char log_string[] = "LOG ";
		int l_cnt = 0;							//iterates through log_string
		int matches_log = 1;

		if (strlen(command) > strlen(log_string)){
			//check to see if command starts with "LOG "
			while (command[l_cnt] != '\0' && log_string[l_cnt] != '\0'){
				if (command[l_cnt] != log_string[l_cnt]){
					matches_log = 0;
				}
				l_cnt++;
			}
		}
		else{
			matches_log = 0;
		}

		//if neither PERIOD nor LOG command
		if (matches_period == 0 && matches_log == 0){
			process_invalid_command();
		}
		else{
			if (matches_period){
				while (command[p_cnt] != '\0'){
					if (!isdigit(command[p_cnt])){
						process_invalid_command();
					}
					p_cnt++;
				}

				//change period
				change_period(atoi(&command[7]));
			}
		}
	}
}


/*****************/
/* MAIN FUNCTION */
/*****************/

int main (int argc, char* argv[]){
	int opt;

	static struct option long_options[] = {
		{"id", required_argument, 0, 'i'},
		{"host", required_argument, 0, 'h'},
		{"log", required_argument, 0, 'l'},
		{0, 0, 0, 0}
	};

	while((opt = getopt_long(argc, argv, "i:h:l", long_options, NULL)) != -1){
    	switch(opt){
      		case 'i':
      			id_length = strlen(optarg);
      			if (id_length == 9){
      				id_num = optarg;
      			}
      			else{
      				fprintf(stderr, "Error: invalid ID number.\n");
      				exit(1);
      			}
        		break;
        	case 'h':
        		hostname = optarg;
        		break;
        	case 'l':
        		log_filename = optarg;
        		log_arg = 1;
        		break;
      		default:
       			fprintf(stderr, "Error: unrecognized argument.\n");
        		exit(1);
    	}
	}

	//check for port number argument
	if (argc <= 1){
		fprintf(stderr, "Error: port number required.\n");
		exit(1);
	}

	//set port number (last argument)
	if ((port_num = atoi(argv[optind])) == 0){
		fprintf(stderr, "Error: invalid port number. %s\n", strerror(errno));
		exit(1);
	}

	//create log file
	if (log_arg == 1){
		//give everyone R/W permissions
        log_fd = open(log_filename, O_WRONLY | O_TRUNC | O_CREAT, 0666);
        if (log_fd == -1){
                fprintf(stderr, "Error creating log file. %s.\n", strerror(errno));
                exit(1);
        }
	}

	//open TCP connection to server
	//IPv4 protocol, TCP connection, Internet Protocol
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_fd == -1){
		fprintf(stderr, "Error creating socket. %s\n", strerror(errno));
		exit(2);
	}

	//create memory for serv_address struct
	memset((char *) &serv_address, 0, sizeof(serv_address));
	serv_address.sin_family = AF_INET;
	serv_address.sin_port = htons(port_num);	//converts port_num from host byte order to network byte order

	//get host info based on host name
	serv_info = gethostbyname(hostname);
	if (serv_info == NULL){
		fprintf(stderr, "Error getting host. %s\n", strerror(errno));
		exit(2);
	}

	//copy memory from serv_info struct to serv_address sin_addr struct
	memcpy((char *) &serv_address.sin_addr.s_addr, (char *) serv_info->h_addr, serv_info->h_length);

	//connect to server
	if ((connect(sock_fd, (struct sockaddr *) &serv_address, sizeof(serv_address))) == -1){
		fprintf(stderr, "Error connecting to server. %s\n", strerror(errno));
		exit(2);
	}

	//send and log ID
	dprintf(sock_fd, "ID=%s\n", id_num);
	if (log_arg){
		dprintf(log_fd, "ID=%s\n", id_num);
	}

	//initialize temperature sensor
	temp_sensor = mraa_aio_init(1);							//A0 corresponds to pin 1

	//initialize time struct
	struct tm* time_values;									//struct that includes values like sec, mins, hours, day of month, etc.
	time_t curr_time;										//current time
	time_t p_start, p_end;									//start of period time, end of period time
	char time_string[10];									//holds properly formatted time in HH:MM:SS

	//initialize poll struct
	pollfd_array[0].fd = sock_fd;							//receive commands over socket connection
	pollfd_array[0].events = POLLIN | POLLHUP | POLLERR;

	int poll_rv;											//holds return value of poll()

	while (1){
		//read input temp
		input_temp = mraa_aio_read(temp_sensor);
		if (input_temp == -1){
			fprintf(stderr, "Error while reading input temperature. %s.\n", strerror(errno));
			exit(1);
		}
		//convert to input temperature to celsius/fahrenheit temperature
		float converted_temp = temp_converter(input_temp, scale_opt);

		//get time
		time_t time_ret = time(&curr_time);
		if (time_ret == -1){
			fprintf(stderr, "Error getting current time.\n");
			exit(1);
		}
		time_values = localtime(&curr_time);				//converts time in timer into local time
		strftime(time_string, 10, "%H:%M:%S", time_values);	//formats time and places into time_string array

		//create report for each sample
		dprintf(sock_fd, "%s %.1f\n", time_string, converted_temp);	//write report to socket
		if (log_arg){
			dprintf(log_fd, "%s %.1f\n", time_string, converted_temp);	//write report to log file
		}

		//check if period taken place
		time_ret = time(&p_start);
		if (time_ret == -1){
			fprintf(stderr, "Error getting start time.\n");
			exit(1);
		}
		time_ret = time(&p_end);
		if (time_ret == -1){
			fprintf(stderr, "Error getting end time.\n");
			exit(1);
		}


		double time_passed = difftime(p_end, p_start);			//gets difference between start and end time
		while (time_passed < period_duration){
			//check if input read from socket
			poll_rv = poll(pollfd_array, 1, 0);
			if (poll_rv < 0){
				fprintf(stderr, "Error while polling. %s.\n", strerror(errno));
				exit(1);
			}

			//if input available
			if (pollfd_array[0].revents & POLLIN){
				//holds characters stored from keyboard input
	            char buff[256];
	            memset(buff, 0, 256*sizeof(char));				//refill buffer with 0 so doesn't have leftover input from previous read

	            int bytesRead = read(sock_fd, &buff, 256);
	            if (bytesRead < 0){
	                    fprintf(stderr, "Error reading from socket. %s.\n", strerror(errno));
	                    exit(1);
	            }

	            char* comm = strtok(buff, "\n");				//breaks buff into strings when newline read

	            //process commands until no commands left
	            while (comm != NULL && bytesRead > 0){
	            	//process command
	            	perform_command(comm);
	            	//log command
	            	if (log_arg && valid_command){
	            		dprintf(log_fd, "%s\n", comm);
	            	}

	            	comm = strtok(NULL, "\n");					//keep reading from buff, if no input left set to NULL
	            	valid_command = 1;							//assume next command is valid
	            }
	        }

	        //if stop flag set,stop generating reports but keep generating input
	        if (!stop){
	        	time_ret = time(&p_end);
				if (time_ret == -1){
					fprintf(stderr, "Error getting end time.\n");
					exit(1);
				}
	        }

	        //update time passed
	        time_passed = difftime(p_end, p_start);
		}
	}

	//close sock_fd
	if (close(sock_fd) == -1){
		fprintf(stderr, "Error: unable to close socket. %s\n", strerror(errno));
		exit(2);
	}

	//close temperature sensor
	mraa_result_t status = mraa_aio_close(temp_sensor);
	if (status != MRAA_SUCCESS) {
		fprintf(stderr, "Error: unable to close temperature sensor. %s\n", strerror(errno));
		exit(2);
	}

	exit(0);
}