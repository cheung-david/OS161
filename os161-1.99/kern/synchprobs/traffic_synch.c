#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

// Store a list of vehicles currently in intersection
struct array* vehicleList; 

// Structure used to organize vehicles
typedef struct Vehicle
{
  Direction origin;
  Direction destination;
} Vehicle;


static struct lock *intersectionLock;
static struct cv *intersectionCV;

// Helper function to enumerate whether a given vehicle is making a right turn
// Returns true if vehicle is making a right turn, false otherwise
bool
right_turn(Vehicle *v) {
  KASSERT(v != NULL);
  if (((v->origin == west) && (v->destination == south)) ||
      ((v->origin == south) && (v->destination == east)) ||
      ((v->origin == east) && (v->destination == north)) ||
      ((v->origin == north) && (v->destination == west))) {
    return true;
  } else {
    return false;
  }
}

// Checks if there are any collisions/conflicts for the given vehichle in the intersection
// Returns true if there is a collision, false otherwise 
bool
check_conflicts_in_intersection(Vehicle *v) {
  for(int i = 0; i < array_num(vehicleList); i++) {
      Vehicle* curVehicle = array_get(vehicleList, i);
      if (curVehicle->origin == v->origin) continue;
      /* no conflict if vehicles go in opposite directions */
      if ((curVehicle->origin == v->destination) &&
          (curVehicle->destination == v->origin)) continue;
      /* no conflict if one makes a right turn and 
         the other has a different destination */
      if ((right_turn(curVehicle) || right_turn(v)) &&
    (v->destination != curVehicle->destination)) continue;
      // There is a conflict
      return true;
  }
  return false;
}

// Looks for given car in intersection and remove it as it is leaving the intersection
void
remove_car_in_intersection(Vehicle *v) {
  for(int i = 0; i < array_num(vehicleList); i++) {
    Vehicle* curVehicle = array_get(vehicleList, i);
    if(curVehicle->origin == v->orign && 
        curVehicle->destination == v->destination) {
        array_remove(vehicleList, i);
        cv_broadcast(intersectionCV, intersectionLock);
        return;
    }
  }
}
/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  intersectionLock = lock_create("intersectionLock");
  if (intersectionLock == NULL) {
    panic("could not create intersection lock");
  }
  intersectionCV = cv_create("intersectionCV");
  if (intersectionCV == NULL) {
    panic("could not create condition variable");
  }
  vehicleList = array_create();
  array_init(vehicleList);

  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(intersectionLock != NULL);
  KASSERT(intersectionCV != NULL);
  lock_destroy(intersectionLock);
  cv_destroy(intersectionCV);
  array_cleanup(vehicleList);
  array_destroy(vehicleList);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  KASSERT(intersectionLock != NULL);
  KASSERT(intersectionCV != NULL);

  lock_acquire(intersectionLock);
  Vehicle *vehicle = kmalloc(sizeof(struct Vehicle));
  if (vehicle == NULL) {
    panic("Vehicle failed to initialize");
  }
  vehicle->origin = origin;
  vehicle->destination = destination;

  while(check_conflicts_in_intersection(vehicle)) {
    cv_wait(intersectionCV, intersectionLock);
  };

  lock_release(intersectionLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  KASSERT(intersectionLock != NULL);
  KASSERT(intersectionCV != NULL);

  lock_acquire(intersectionLock);
  Vehicle *vehicle = kmalloc(sizeof(struct Vehicle));
  if (vehicle == NULL) {
    panic("Vehicle failed to initialize");
  }
  vehicle->origin = origin;
  vehicle->destination = destination;
  remove_car_in_intersection(vehicle);
  lock_release(intersectionLock);
}
