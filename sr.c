#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 12
#define NOTINUSE -1

struct pkt send_buffer[SEQSPACE];
int send_base;
int next_seqnum;
bool acked[SEQSPACE];
float send_times[SEQSPACE];

struct pkt recv_buffer[SEQSPACE];
bool received[SEQSPACE];
int expected_seqnum;

int ComputeChecksum(struct pkt packet) {
    int checksum = packet.seqnum + packet.acknum;
    int i;
    for (i = 0; i < 20; i++) {
        checksum += (int)(packet.payload[i]);
    }
    return checksum;
}

bool IsCorrupted(struct pkt packet) {
    return packet.checksum != ComputeChecksum(packet);
}

void A_output(struct msg message) {
    struct pkt pkt;
    int i;

    if ((next_seqnum + SEQSPACE - send_base) % SEQSPACE >= WINDOWSIZE) {
        if (TRACE > 0)
            printf("----A: window full, message dropped\n");
        window_full++;
        return;
    }

    pkt.seqnum = next_seqnum;
    pkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
        pkt.payload[i] = message.data[i];
    pkt.checksum = ComputeChecksum(pkt);

    send_buffer[next_seqnum] = pkt;
    acked[next_seqnum] = false;
    send_times[next_seqnum] = get_sim_time();
    tolayer3(A, pkt);
    if (TRACE > 0)
        printf("----A: sent packet %d\n", pkt.seqnum);

    packets_sent++;
    starttimer(A, RTT);
    next_seqnum = (next_seqnum + 1) % SEQSPACE;
}

void A_input(struct pkt packet) {
    if (IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----A: received corrupted ACK\n");
        return;
    }

    total_ACKs_received++;

    int ack = packet.acknum;
    if (!acked[ack]) {
        acked[ack] = true;
        new_ACKs++;
        if (TRACE > 0)
            printf("----A: ACK %d received\n", ack);
    }

    while (acked[send_base]) {
        acked[send_base] = false;
        send_base = (send_base + 1) % SEQSPACE;
    }

    stoptimer(A);
    if (send_base != next_seqnum)
        starttimer(A, RTT);
}

void A_timerinterrupt(void) {
    int i;

    if (TRACE > 0)
        printf("----A: timer interrupt, checking for resends\n");

    for (i = 0; i < SEQSPACE; i++) {
        float now = get_sim_time();
        if (((now - send_times[i]) >= RTT) && 
            ((next_seqnum + SEQSPACE - send_base) % SEQSPACE < WINDOWSIZE) &&
            !acked[i]) {

            tolayer3(A, send_buffer[i]);
            send_times[i] = now;
            packets_resent++;
            if (TRACE > 0)
                printf("----A: resent packet %d\n", i);
        }
    }

    starttimer(A, RTT);
}

void A_init(void) {
    int i;
    send_base = 0;
    next_seqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        acked[i] = false;
        send_times[i] = 0.0;
    }
}

/********* Receiver *********/

void B_input(struct pkt packet) {
    struct pkt ackpkt;
    int i;

    if (IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----B: received corrupted packet\n");
        return;
    }

    int seq = packet.seqnum;

    if (!received[seq]) {
        recv_buffer[seq] = packet;
        received[seq] = true;
        if (TRACE > 0)
            printf("----B: packet %d received and buffered\n", seq);
    }

    while (received[expected_seqnum]) {
        tolayer5(B, recv_buffer[expected_seqnum].payload);
        received[expected_seqnum] = false;
        expected_seqnum = (expected_seqnum + 1) % SEQSPACE;
    }

    ackpkt.seqnum = 0;
    ackpkt.acknum = seq;
    for (i = 0; i < 20; i++)
        ackpkt.payload[i] = 0;
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);
    if (TRACE > 0)
        printf("----B: sent ACK %d\n", seq);

    packets_received++;
}

void B_init(void) {
    int i;
    expected_seqnum = 0;
    for (i = 0; i < SEQSPACE; i++)
        received[i] = false;
}

void B_output(struct msg message) { }
void B_timerinterrupt(void) { }

