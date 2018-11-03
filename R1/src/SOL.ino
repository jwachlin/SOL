#include "SOL.h"

void setup() {
  // Perform setup
  SOL_begin();

  // Run task
  SOL_task();

}

void loop() {
  // Uses deep sleep modes, so never reaches here
}
