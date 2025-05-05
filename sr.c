#include <stdio.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define WINDOWSIZE 6
#define SEQSPACE 2*WINDOWSIZE
#define NOTINUSE (-1)
#define RTT 16.0

static struct pkt window[SEQSPACE];
static int acked[SEQSPACE];
static float send_times[SEQSPACE];
static int send_base, next_seqnum;

static int expected_seqnum;
static int received[SEQSPACE];
static struct pkt buffer[SEQSPACE];

int ComputeChecksum(struct pkt packet) {
    int checksum = 0;
    int i;
    checksum += packet.seqnum;
    checksum += packet.acknum;
    for (i = 0; i < 20; i++) {
        checksum += (int)(packet.payload[i]);
    }
    return checksum;
}

int IsCorrupted(struct pkt packet) {
    return packet.checksum != ComputeChecksum(packet);
}

void A_output(struct msg message) {
    struct pkt packet;
    int i;

    if ((next_seqnum + SEQSPACE - send_base) % SEQSPACE < WINDOWSIZE) {
        packet.seqnum = next_seqnum;
        packet.acknum = NOTINUSE;
        for (i = 0; i < 20; i++) {
            packet.payload[i] = message.data[i];
        }
        packet.checksum = ComputeChecksum(packet);

        window[next_seqnum] = packet;
        acked[next_seqnum] = 0;
        send_times[next_seqnum] = get_sim_time();

        tolayer3(A, packet);
        if ((send_base == next_seqnum)) {
            starttimer(A, RTT);
        }

        next_seqnum = (next_seqnum + 1) % SEQSPACE;
        packets_sent++;
    } else {
        window_full++;
    }
}

void A_input(struct pkt packet) {
    int ack = packet.acknum;

    if (!IsCorrupted(packet)) {
        if (!acked[ack]) {
            acked[ack] = 1;
            new_ACKs++;
        }
        total_ACKs_received++;

        while (acked[send_base]) {
            acked[send_base] = 0;
            send_base = (send_base + 1) % SEQSPACE;
        }

        stoptimer(A);
        if (send_base != next_seqnum) {
            starttimer(A, RTT);
        }
    }
}

void A_timerinterrupt(void) {
    int i;
    for (i = 0; i < SEQSPACE; i++) {
        if (!acked[i] && ((get_sim_time() - send_times[i]) >= RTT)) {
            tolayer3(A, window[i]);
            packets_resent++;
            send_times[i] = get_sim_time();
        }
    }
    starttimer(A, RTT);
}

void A_init(void) {
    int i;
    send_base = 0;
    next_seqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        acked[i] = 0;
    }
}

void B_input(struct pkt packet) {
    struct pkt ackpkt;
    int i;
    int seq = packet.seqnum;

    if (!IsCorrupted(packet)) {
        if (!received[seq]) {
            for (i = 0; i < 20; i++) {
                buffer[seq].payload[i] = packet.payload[i];
            }
            buffer[seq].seqnum = packet.seqnum;
            buffer[seq].acknum = packet.acknum;
            buffer[seq].checksum = packet.checksum;
            received[seq] = 1;
            packets_received++;
        }

        while (received[expected_seqnum]) {
            tolayer5(B, buffer[expected_seqnum].payload);
            received[expected_seqnum] = 0;
            expected_seqnum = (expected_seqnum + 1) % SEQSPACE;
        }
    }

    ackpkt.seqnum = 0;
    ackpkt.acknum = packet.seqnum;
    for (i = 0; i < 20; i++) {
        ackpkt.payload[i] = 0;
    }
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);
}

void B_init(void) {
    int i;
    expected_seqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        received[i] = 0;
    }
}

