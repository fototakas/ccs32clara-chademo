/* Interface header for hardwareInterface.c */

/* Global Defines */
#define CAN_TIMEOUT 30 //multiples of 100ms so 3 seconds

enum LockStt
{
   LOCK_UNKNOWN, LOCK_OPEN, LOCK_CLOSED, LOCK_OPENING, LOCK_CLOSING
};

/* Global Variables */

/* Global Functions */
#ifdef __cplusplus
extern "C" {
#endif

extern void hardwareInterface_setStateB(void);
extern void hardwareInterface_setStateC(void);
extern void hardwareInterface_setPowerRelayOff(void);
extern void hardwareInterface_setPowerRelayOn(void);
extern void hardwareInterface_triggerConnectorLocking(void);
extern void hardwareInterface_triggerConnectorUnlocking(void);
extern uint8_t hardwareInterface_isConnectorLocked(void);
extern uint8_t hardwareInterface_getPowerRelayConfirmation(void);
extern bool hardwareInterface_stopChargeRequested();
extern uint8_t hardwareInterface_getIsAccuFull(void);
extern uint8_t hardwareInterface_getSoc(void);
extern int16_t hardwareInterface_getAccuVoltage(void);
extern int16_t hardwareInterface_getInletVoltage(void);
extern int16_t hardwareInterface_getChargingTargetVoltage(void);
extern int16_t hardwareInterface_getChargingTargetCurrent(void);


#ifdef __cplusplus
}
#endif
