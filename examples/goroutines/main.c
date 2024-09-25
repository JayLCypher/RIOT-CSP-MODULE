// Recreation of the go example from https://gobyexample.com/goroutines
#include "csp.h" // CSP

#include <stdio.h> // fmt
#include "ztimer.h" // time

void *f(const char *from) {
  for (int i = 0; i != 3; ++i) {
    printf("%s : %d\n", from, i);
    // Mimic go runtime cooperative function call for print
    ztimer_sleep(ZTIMER_MSEC, 1);
  }
  return nullptr;
}

// C does not have lambdas yet.
void *lambda(const char *msg) {
  printf("%s\n", msg);
  return nullptr;
}

int main(void) {
  f("direct");
  
  GO(f, "goroutine?");
  
  GO(lambda, "going");
  
  ztimer_sleep(ZTIMER_MSEC, 1000); // 1s
  puts("done");
}
