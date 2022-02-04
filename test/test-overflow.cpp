#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include <new>
#include <vector>
#include <future>
#include <iostream>

#include <thread>
#include <assert.h>
static void block_overflow1(void) {
  uint8_t* p = (uint8_t*)malloc(17);
  p[18] = 0;
  free(p);
  uint8_t* q = (uint8_t*)malloc(17);
  free(p);
  free(q);
}

#define OVF_SIZE 100

static void block_overflow2(void) {
  uint8_t* p = (uint8_t*)malloc(30);
  memset(p+30, 0, OVF_SIZE);
  free(p);
}

int main() {
  printf("test overflow..\n");
  block_overflow1();
  block_overflow2(); 
  printf("done..\n");
  return 0;
}
