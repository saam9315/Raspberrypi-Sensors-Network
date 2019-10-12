#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "grovepi.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pwd.h>
#include <signal.h>

#define SERVERLOCK "/home/moxdlab/team39/cprogramme/projekt/serverlock"

int fileLock;

void disp(key_t msgQKey);
void sigintHandler();



#define INPUT 0
#define OUTPUT 1

struct text_message {
	long mtype;
	char mtext[100];
};


typedef struct PEER {

	char ip[64];
	char connectionTime[64];
	int port;
	int connected;

	float tempMax;
	float tempCur;
	float tempMin;


	float humMax;
	float humCur;
	float humMin;


	float lightMax;
	float lightCur;
	float lightMin;


	float soundMax;
	float soundCur;
	float soundMin;



} PEER;

struct saveValues {
	int peersCount;
	struct PEER myData;
};


int main() {

	//server variables
	int sock, newSock, cli_len, pid1, msid, v;
	struct sockaddr_in server_addr, client_addr;
	struct text_message mess;
	int port = 7654;
	int maxClients = 10;
	struct saveValues *sharedValues;

	fileLock = 0;

	//int fileLock;

	struct stat statbuf;

	if ((fileLock = open(SERVERLOCK, O_WRONLY | O_CREAT | O_EXCL, 0644)) == -1) {
		printf("Sorry, %s is already locked\n", SERVERLOCK);
		stat(SERVERLOCK, &statbuf);
		//printf("%s\n", getpwuid(statbuf.st_uid)->pw_nam);
		return -1;
	} else {

		signal(SIGINT, sigintHandler);






		//semaphor
		unsigned short marker[1];
		int sem_id = semget (IPC_PRIVATE, 1, IPC_CREAT | 0644);
		marker[0] = 1;
		semctl(sem_id, 1, SETALL, marker);
		struct sembuf enter, leave; // Structs für den Semaphor
		enter.sem_num = leave.sem_num = 0; // Semaphor 0 in der Gruppe
		enter.sem_flg = leave.sem_flg = SEM_UNDO;
		enter.sem_op = -1; // blockieren, DOWN-Operation
		leave.sem_op = 1; // freigeben, UP-Operation */


		//shared memory
		int shmid;
		int shmidPC; //shmid for peersCount

		shmidPC = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);

		sharedValues = shmat(shmidPC, NULL, 0);

		sharedValues->peersCount = 0;
		strcpy(sharedValues->myData.ip, "0.0.0.0");
		sharedValues->myData.port = 0000;

		time_t t = time(NULL);
		struct tm *tm = localtime(&t);
		assert(strftime(sharedValues->myData.connectionTime, sizeof(sharedValues->myData.connectionTime), "%c", tm));


		shmid = shmget(IPC_PRIVATE, maxClients * sizeof(PEER), IPC_CREAT | 0666);
		PEER *peersList;
		peersList = shmat(shmid, NULL, 0);


		//msg queue
		key_t msgQKey = ftok("progfile", 65);
		msid = msgget(msgQKey, 0666 | IPC_CREAT);
		if (msid == -1) {
			perror("cannot get message queue\n");
			exit(1);
		}





		//Anlegen eines Sockets
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0) {
			perror("ERROR opening the socket\n");
			exit(1);
		}
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = INADDR_ANY;
		server_addr.sin_port = htons(port);


		int option = 1;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));


		if (bind(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
			perror("ERROR on binding the server to an Address\n");
			exit(1);
		}
		//Auf Verbindungen hören
		int k = listen(sock, 5);
		if (k < 0)
			perror("error \n");


		cli_len = sizeof(client_addr);

		if (init() == -1)
			perror("Failed to initialize the Pie");





		while (1) {

			newSock = accept(sock, (struct sockaddr *) &client_addr, &cli_len);


			if (newSock < 0) {
				perror("ERROR on accepting a connection");
				exit(1);
			}


			//Display the message
			mess.mtype = 20;
			sprintf(mess.mtext, "CONNECTED\n%s", inet_ntoa(client_addr.sin_addr));
			//printf("%s\n", mess.mtext );
			v = msgsnd(msid, &mess, sizeof(mess), 20);
			if (v < 0) {
				perror("error writing to queue\n");
			}
			else
				disp(msgQKey);

			semop(sem_id, &enter, 1);
			strcpy(peersList[sharedValues->peersCount].ip, inet_ntoa(client_addr.sin_addr));
			peersList[sharedValues->peersCount].port = ntohs(client_addr.sin_port);
			peersList[sharedValues->peersCount].connected = 1;

			assert(strftime(peersList[sharedValues->peersCount].connectionTime, sizeof(peersList[sharedValues->peersCount].connectionTime), "%c", tm));
			if (sharedValues->peersCount < maxClients) {
				sharedValues->peersCount++;
				semop(sem_id, &leave, 1);
			}
			else {
				perror("Max Clients number reached\n");
				semop(sem_id, &leave, 1);
			}






			pid1 = fork();
			if (pid1 < 0) {
				perror("ERROR on fork\n");
				exit(1);
			}
			if (pid1 == 0)  { //child process
				close(sock);

				while (1) {

					char request[100];
					char out[40];

					int n = recv(newSock, request, 50, 0);
					char *res;

					if (strstr(request, "GET TEMPERATURE") != NULL) {
						int portnum = 8; //Sensor an digital Port D8 anschließen
						float temp = 0;
						pinMode(portnum, INPUT);
						getTemperature(&temp, portnum);
						if (temp < -40 || temp > 80) {
							send(newSock, "void\n", 5, 0);
						}
						else {
							sprintf(out, "%s%2.2f\n\n", "TEMPERATURE=", temp);
							send(newSock, out, 19, 0);
						}

					}

					if (strstr(request, "GET HUMIDITY") != NULL) {
						//bzero((char *) &request,sizeof(request));
						//getHum(newSock);
						int portnum = 8; //Sensor an digital Port D8 anschließen
						float humidity = 0;
						pinMode(portnum, INPUT);
						getHumidity(&humidity, portnum);
						if (humidity < 5 || humidity < 99) {
							//send(newSock, "void\n", 5, 0);
							sprintf(out, "%s%2.2f%\n\n", "HUMIDITY=", humidity);
							send(newSock, out, 17, 0);
						}
						else {
							sprintf(out, "%s%2.2f%\n\n", "HUMIDITY=", humidity);
							send(newSock, out, 17, 0);
						}
					}

					if (strstr(request, "GET LIGHT") != NULL) {

						int portnum = 0; //connect the light sensor to this port
						int led = 3; //connect a LED to this port
						int value;
						float resistance;
						//threshhold to pass
						int threshhold = 10;

						pinMode(portnum, INPUT);
						pinMode(led, OUTPUT);


						//read from the analog Port
						value = analogRead(portnum);
						//calculate the resistance of the light sensor
						resistance = (float)(1023 - value) * 10 / value;
						//if the resitance passes threshhold, turn on the LED
						if (resistance > threshhold)
							digitalWrite(led, 1);
						else
							digitalWrite(led, 0);

						sprintf(out, "LIGHT=%d\n\n", value);

						send(newSock, out, 11, 0);


					}

					if (strstr(request, "GET SOUND") != NULL) {
						int portnum = 1; //Audio-Sensor an analogen Port A1 anschließen
						int sound = 0; //Lautstärke



						pinMode(portnum, INPUT); //Modus festlegen (pinMode([PinNr.],[0(INPUT)/1(OUTPUT)])


						//	while(1){
						sound = analogRead(portnum); //Frage Port A1 ab und speichere Wert in Variable
						sprintf(out, "%s%d\n\n", "SOUND=", sound);

						send(newSock, out, 11, 0);
					}

					if (strstr(request, "EXIT") != NULL) {
						send(newSock, "CONNECTION CLOSED!\n\n", 21, 0);
						semop(sem_id, &enter, 1);
						int n;
						for (int i = 0; i < sharedValues->peersCount; i++) {
							if (strstr(peersList[i].ip, inet_ntoa(client_addr.sin_addr)) != NULL && peersList[i].port == ntohs(client_addr.sin_port)) {
								n = i;
								peersList[n].connected = 0;
								//printf("Found it\n");
							}

						}


						semop(sem_id, &leave, 1);
						close(newSock);
						mess.mtype = 5;
						sprintf(mess.mtext, "DISCONNECTED\n%s", inet_ntoa(client_addr.sin_addr));
						//printf("%s\n", mess.mtext );
						v = msgsnd(msid, &mess, sizeof(mess), 0);
						if (v < 0) {
							perror("error writing to queue\n");
						}
						//else
						//	disp(msgQKey);



						exit(1);
					}
					if (strstr(request, "GET TEMPERATURE") == NULL && strstr(request, "PEERS") == NULL && strstr(request, "GET HUMIDITY") == NULL && strstr(request, "GET LIGHT") == NULL && strstr(request, "GET SOUND") == NULL && strstr(request, "GET EXIT") == NULL) {
						send(newSock, "INVALID COMMAND!\n", 17, 0);

					}

				}



			}//parent process
			else {


				close(newSock);

				int pid2 = fork();
				if (pid2 < 0) {
					perror("ERROR on fork\n");
					exit(1);
				}
				if (pid2 == 0)  {
					int pid3;
					struct sockaddr_in peer_addr;
					int peerPort = 5678;
					int sockfd;

					while (1) {

						char ordr[100];
						char peerIP[64];
						fflush(stdout);
						fgets(ordr, sizeof(ordr), stdin);
						fflush(stdin);

						/*if (strstr(ordr, "EXIT") != NULL) {
							printf("SERVER CLOSED!\n\n");

							shmdt(peersList);
							shmdt(sharedValues);
							shmctl(shmid, IPC_RMID, 0);
							shmctl(shmidPC, IPC_RMID, 0);
							semctl(sem_id, 0, IPC_RMID);
							msgctl(msid, IPC_RMID, NULL);
							unlink(fileLock);
							exit(0);
						}*/



						//Peersliste ausgeben
						if (strstr(ordr, "PEERS") != NULL) {


							printf("Timestamp:\t\t\tip-Address:\t\tPort:\t\tTemp. (min/cur/max):\t\tHumidity (min/cur/max):\t\tLight (min/cur/max):\t\tSound (min/cur/max):\n");
							semop(sem_id, &enter, 1);

							for (int i = 0; i < sharedValues->peersCount; i++ ) {
								if (peersList[i].connected == 1) {
									if (peersList[i].tempMin == 0.00 && peersList[i].tempCur == 0.00 && peersList[i].tempMax == 0.00 && peersList[i].humMin == 0.00 && peersList[i].humCur == 0.00 && peersList[i].humMax == 0.00 &&
									        peersList[i].lightMin == 0.00 && peersList[i].lightCur == 0.00 && peersList[i].lightMax == 0.00 && peersList[i].soundMin == 0.00 && peersList[i].soundCur == 0.00 && peersList[i].soundMax == 0.00)
										printf("%s\t%s\t\t%d\t\tUA\t\t\t\tUA\t\t\t\tUA\t\t\t\tUA\n", peersList[i].connectionTime, peersList[i].ip,
										       peersList[i].port);

									else
										printf("%s\t%s\t\t%d\t\t%.02f/%.02f/%.02f\t\t%.02f/%.02f/%.02f\t\t%.02f/%.02f/%.02f\t\t%.02f/%.02f/%.02f\n", peersList[i].connectionTime, peersList[i].ip,
										       peersList[i].port, peersList[i].tempMin, peersList[i].tempCur , peersList[i].tempMax, peersList[i].humMin, peersList[i].humCur, peersList[i].humMax,
										       peersList[i].lightMin, peersList[i].lightCur, peersList[i].lightMax, peersList[i].soundMin, peersList[i].soundCur, peersList[i].soundMax);

								}

							}
							semop(sem_id, &leave, 1);

							printf("%s\t%s\t\t\t%d\t\t%.02f/%.02f/%.02f\t\t%.02f/%.02f/%.02f\t\t%.02f/%.02f/%.02f\t\t%.02f/%.02f/%.02f\n", sharedValues->myData.connectionTime, sharedValues->myData.ip,
							       sharedValues->myData.port, sharedValues->myData.tempMin, sharedValues->myData.tempCur , sharedValues->myData.tempMax, sharedValues->myData.humMin, sharedValues->myData.humCur,
							       sharedValues->myData.humMax, sharedValues->myData.lightMin, sharedValues->myData.lightCur, sharedValues->myData.lightMax, sharedValues->myData.soundMin,
							       sharedValues->myData.soundCur, sharedValues->myData.soundMax);

						}



						if (strstr(ordr, "CONNECT") != NULL) {

							printf("Please enter the ip-Address:\n");

							fflush(stdout);
							fgets(peerIP, sizeof(peerIP), stdin);
							fflush(stdin);


							sockfd = socket(AF_INET, SOCK_STREAM, 0);
							peer_addr.sin_family = AF_INET;
							peer_addr.sin_port = htons(peerPort);
							//inet_pton(AF_INET, peerIP, &(peer_addr.sin_addr));
							peer_addr.sin_addr.s_addr = inet_addr(peerIP);

							//printf("%s\n", peerIP );
							//printf("%x\n", peer_addr.sin_addr);
							//printf("%d\n", connect(sockfd, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) );

							if (connect(sockfd, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) == -1) {
								perror("Connection failed");

							}
							else {
								mess.mtype = 20;
								sprintf(mess.mtext, "CONNECTED\n%s", peerIP);
								printf("%s\n", mess.mtext );
								v = msgsnd(msid, &mess, sizeof(mess), 20);
								if (v < 0) {
									printf("error writing to queue\n");
								}
								else
									disp(msgQKey);

								int currentIndex;
								for (int i = 0; i < sharedValues->peersCount; i++) {
									if (strstr(peersList[i].ip, peerIP) != NULL)
										currentIndex = i;
								}

								currentIndex = sharedValues->peersCount;
								semop(sem_id, &enter, 1);
								peersList[sharedValues->peersCount].connected = 1;
								sharedValues->peersCount++;
								semop(sem_id, &leave, 1);
								strcpy(peersList[currentIndex].ip, inet_ntoa(peer_addr.sin_addr));
								peersList[currentIndex].port = ntohs(peer_addr.sin_port);

								time_t t = time(NULL);
								struct tm *tm = localtime(&t);
								assert(strftime(peersList[currentIndex].connectionTime, sizeof(peersList[currentIndex].connectionTime), "%c", tm));


								pid3 = fork();

								if (pid3 < 0) {
									printf("ERROR on fork\n");
									exit(0);
								}
								else if (pid3 == 0) {


									while (1) {
										char answer[100];
										char val[6];
										char key[] = "="; // char to look for in string received from peer
										bzero((char *) &answer, sizeof(answer));
										bzero((char *) &val, sizeof(val));
										//TEMPERATURE
										send(sockfd, "GET TEMPERATURE", 15, 0);
										recv(sockfd, answer, sizeof(answer), 0);
										int i = strcspn (answer, key);
										if (i == strlen(answer)) {
											perror("Error on getting current TEMPERATURE");
											break;
										}

										else {
											memcpy( val, &answer[i + 1], 5);
											peersList[currentIndex].tempCur = atof(val);
											if (peersList[currentIndex].tempCur < peersList[currentIndex].tempMin)
												peersList[currentIndex].tempMin = peersList[currentIndex].tempCur;

											else if (peersList[currentIndex].tempCur > peersList[currentIndex].tempMax)
												peersList[currentIndex].tempMax = peersList[currentIndex].tempCur;
										}

										//Fill my own Temperature Data
										int portnum = 8; //Sensor an digital Port D8 anschließen
										float temp = 0;
										pinMode(portnum, INPUT);
										getTemperature(&temp, portnum);
										if (temp < -40 || temp > 80) {
											perror("Error getting own TEMPERATURE Values");
										}
										else {
											sharedValues->myData.tempCur = temp;
											if (sharedValues->myData.tempCur < sharedValues->myData.tempMin)
												sharedValues->myData.tempMin = sharedValues->myData.tempCur;
											else if (sharedValues->myData.tempCur > sharedValues->myData.tempMax)
												sharedValues->myData.tempMax = sharedValues->myData.tempCur;

										}

										bzero((char *) &answer, sizeof(answer));
										bzero((char *) &val, sizeof(val));
										//HUMIDITY
										send(sockfd, "GET HUMIDITY", 12, 0);
										recv(sockfd, answer, sizeof(answer), 0);
										i = strcspn (answer, key);
										if (i == strlen(answer)) {
											perror("Error on getting current HUMIDITY");
											break;
										}

										else {
											memcpy( val, &answer[i + 1], 5);
											peersList[currentIndex].humCur = atof(val);
											if (peersList[currentIndex].humCur < peersList[currentIndex].humMin )
												peersList[currentIndex].humMin = peersList[currentIndex].humCur;

											else if ( peersList[currentIndex].humCur > peersList[currentIndex].humMax )
												peersList[currentIndex].humMax = peersList[currentIndex].humCur;

										}

										//Fill my own Humidity Data
										portnum = 8; //Sensor an digital Port D8 anschließen
										float humidity = 0;
										pinMode(portnum, INPUT);
										getHumidity(&humidity, portnum);

										sharedValues->myData.humCur = humidity;
										if (sharedValues->myData.humCur < sharedValues->myData.humMin)
											sharedValues->myData.humMin = sharedValues->myData.humCur;

										else if (sharedValues->myData.humCur > sharedValues->myData.humMax)
											sharedValues->myData.humMax = sharedValues->myData.humCur;





										bzero((char *) &answer, sizeof(answer));
										bzero((char *) &val, sizeof(val));
										//HUMIDITY
										send(sockfd, "GET LIGHT", 9, 0);
										recv(sockfd, answer, sizeof(answer), 0);
										i = strcspn (answer, key);
										if (i == strlen(answer)) {
											perror("Error on getting current LIGHT");
											break;
										}

										else {
											memcpy( val, &answer[i + 1], 5);
											peersList[currentIndex].lightCur = atof(val);

											if (peersList[currentIndex].lightCur < peersList[currentIndex].lightMin)
												peersList[currentIndex].lightMin = peersList[currentIndex].lightCur;

											else if (peersList[currentIndex].lightCur > peersList[currentIndex].lightMax)
												peersList[currentIndex].lightMax = peersList[currentIndex].lightCur;

										}

										//Fill my own Light Data

										portnum = 0;
										int lightValue;



										pinMode(portnum, INPUT);


										lightValue = analogRead(portnum);

										sharedValues->myData.lightCur = lightValue;
										if (sharedValues->myData.lightCur < sharedValues->myData.lightMin)
											sharedValues->myData.lightMin = sharedValues->myData.lightCur;

										else if (sharedValues->myData.lightCur > sharedValues->myData.lightMax)
											sharedValues->myData.lightMax = sharedValues->myData.lightCur;





										bzero((char *) &answer, sizeof(answer));
										bzero((char *) &val, sizeof(val));
										//HUMIDITY
										send(sockfd, "GET SOUND", 9, 0);
										recv(sockfd, answer, sizeof(answer), 0);
										i = strcspn (answer, key);
										if (i == strlen(answer)) {
											perror("Error on getting current SOUND");
											break;
										}

										else {
											memcpy( val, &answer[i + 1], 5);
											peersList[currentIndex].soundCur = atof(val);

											if (peersList[currentIndex].soundCur < peersList[currentIndex].soundMin)
												peersList[currentIndex].soundMin = peersList[currentIndex].soundCur;

											else if (peersList[currentIndex].soundCur > peersList[currentIndex].soundMax)
												peersList[currentIndex].soundMax = peersList[currentIndex].soundCur;

										}


										//Filling my own Sound Data
										portnum = 1; //Audio-Sensor an analogen Port A1 anschließen
										int sound = 0; //Lautstärke



										pinMode(portnum, INPUT); //Modus festlegen (pinMode([PinNr.],[0(INPUT)/1(OUTPUT)])
										sound = analogRead(portnum); //Frage Port A1 ab und speichere Wert in Variable

										sharedValues->myData.soundCur = sound;
										if (sharedValues->myData.soundCur < sharedValues->myData.soundMin)
											sharedValues->myData.soundMin = sharedValues->myData.soundCur;
										else if (sharedValues->myData.soundCur > sharedValues->myData.soundMax)
											sharedValues->myData.soundMax = sharedValues->myData.soundCur;





										sleep(10000); // sleep 10 seconds

									}// end of while (filling sensor data)



								}// end of child process (3)
								else {
									close(sockfd);
									//father process (3)
								}

							}// end of connection successful path

						}// order = CONNECT

						if (strstr(ordr, "PEERS") == NULL && strstr(ordr, "CONNECT") == NULL ) {
							printf("INVALID COMMAND!\n");

						}


					}//end of while to check what user enters on terminal


				}// end of child process (2)

				else {
					//father process (2);
				}


			}//end of parent process (1)
		}// end of main while
	}

	return 0;
}


void sigintHandler()
{

	signal(SIGINT, sigintHandler);
	printf("\nTERMINATED\n");
	unlink(SERVERLOCK);
	fflush(stdout);
	(void) signal(SIGINT,SIG_DFL);
}

void disp (key_t msgQKey) {



	int p = fork();

	if (p < 0) {
		perror("Error on Fork");
		exit(1);
	}
	else if (p == 0) {
		int msgid, v;

		msgid = msgget(msgQKey, 0);

		if (msgid == -1) {
			perror("cannot get message queue\n");
			exit(1);
		}

		connectLCD();

		// set text and RGB color on the LCD
		setRGB(0, 204, 255);

		while (1) {
			struct text_message message;
			v = msgrcv(msgid, &message, sizeof(message), 5, IPC_NOWAIT);

			if (v < 0) {
				if (errno == ENOMSG) {
					v = msgrcv(msgid, &message, sizeof(message), 20, IPC_NOWAIT);
					if (v < 0) {
						if (errno == ENOMSG) {
							continue;

						} else {
							perror("error reading from queue\n");
							break;

						}
					} else {
						setText(message.mtext);
						pi_sleep(3000);
					}

				} else {
					perror("error reading from queue\n");
					break;

				}
			} else {
				//printf("%s\n", message.mtext);
				//printf("%d\n", num_messages);
				setText(message.mtext);
				pi_sleep(3000);
			}

		}




	}

	else
	{
		//Father Process
	}



}



