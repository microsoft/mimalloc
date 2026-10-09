#include <thread>

#include <dlfcn.h>

#include "main-static-dep.h"

TestFun fun;
void*   so;

void testso() {
  fun();
}

void loadso() {
  so = dlopen("./libstatic.so", RTLD_LAZY);
  fun = (TestFun)dlsym(so,"Test");
  testso();
}

static void test_static(void) {
  auto t1 = std::thread(&loadso);
  t1.join();
  auto t2 = std::thread(&testso);
  t2.join();
}

int main(int argc, char** argv) {
  test_static();
  return 0;
}
