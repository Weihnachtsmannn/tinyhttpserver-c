/* timer.c */
#include "timer.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static timer_node **g_heap = NULL;
static int g_heap_size = 0;
static int g_heap_capacity = 0;

static void heap_swap(int i, int j) {
        timer_node *tmp = g_heap[i];
        g_heap[i] = g_heap[j];
        g_heap[j] = tmp;
}

static void shift_up(int i) {
        while (i > 0) {
                int parent = (i - 1) / 2;
                if (g_heap[parent]->expire <= g_heap[i]->expire)
                        break;
                heap_swap(parent, i);
                i = parent;
        }
}

static void shift_down(int i) {
        while (1) {
                int left = 2 * i + 1, right = 2 * i + 2, smallest = i;
                if (left < g_heap_size &&
                    g_heap[left]->expire < g_heap[smallest]->expire)
                        smallest = left;
                if (right < g_heap_size &&
                    g_heap[right]->expire < g_heap[smallest]->expire)
                        smallest = right;
                if (smallest == i)
                        break;
                heap_swap(i, smallest);
                i = smallest;
        }
}

void timer_heap_init(int capacity) {
        g_heap_capacity = capacity;
        g_heap = malloc(sizeof(timer_node *) * capacity);
        g_heap_size = 0;
}

timer_node *timer_add(int sockfd, time_t expire, void (*cb)(client_data *),
                      client_data *ud) {
        (void)sockfd;
        if (g_heap_size >= g_heap_capacity)
                return NULL;

        timer_node *node = malloc(sizeof(timer_node));
        node->expire = expire;
        node->callback = cb;
        node->user_data = ud;

        g_heap[g_heap_size] = node;
        shift_up(g_heap_size);
        g_heap_size++;
        return node;
}

void timer_adjust(timer_node *timer, time_t new_expire) {
        if (!timer)
                return;
        timer->expire = new_expire;
        /* 找到节点位置并调整 */
        for (int i = 0; i < g_heap_size; i++) {
                if (g_heap[i] == timer) {
                        shift_up(i);
                        shift_down(i);
                        break;
                }
        }
}

void timer_del(timer_node *timer) {
        if (!timer)
                return;
        for (int i = 0; i < g_heap_size; i++) {
                if (g_heap[i] == timer) {
                        heap_swap(i, g_heap_size - 1);
                        g_heap_size--;
                        free(timer);
                        if (i < g_heap_size) {
                                shift_up(i);
                                shift_down(i);
                        }
                        return;
                }
        }
}

void timer_tick(void) {
        time_t now = time(NULL);
        while (g_heap_size > 0 && g_heap[0]->expire <= now) {
                timer_node *top = g_heap[0];
                top->callback(top->user_data);
                heap_swap(0, g_heap_size - 1);
                g_heap_size--;
                free(top);
                if (g_heap_size > 0)
                        shift_down(0);
        }
}

void timer_destroy(void) {
        for (int i = 0; i < g_heap_size; i++)
                free(g_heap[i]);
        free(g_heap);
        g_heap = NULL;
        g_heap_size = g_heap_capacity = 0;
}