
#include <string.h>
#include <stdint.h>
#include "chademoCharger.h"
#include "params.h"
#include "main.h"

#include <libopencm3/stm32/can.h>

extern ChademoCharger* chademoCharger;

#define low_byte(x)  ((uint8_t)(x))
#define high_byte(x) ((uint8_t)((x) >> 8))
#define max_byte(x) ((x) > 0xFF ? 0xFF : (x))

#define SWAP16(x) (uint16_t)((((x) & 0x00FF) << 8) | (((x) & 0xFF00) >> 8))

// only for bytes
//#define set_flag(x, flag)    ((x) = static_cast<decltype(x)>((x) | (flag)))
//#define clear_flag(x, flag)  ((x) = static_cast<decltype(x)>((x) & ~(flag)))
//#define has_flag(x, flag)    (((x) & (flag)) != static_cast<decltype(x)>(0))
//#define has_flags(x, flags) (((x) & (flags)) == static_cast<decltype(x)>(flags))

#define COMPARE_SET(oldval, newval, fmt) \
    do { \
        if ((oldval) != (newval)) { \
            printf(fmt, (oldval), (newval)); \
            (oldval) = (newval); \
        } \
    } while (0)

extern volatile uint32_t system_millis;
extern global_data _global;


#define LAST_ASKING_FOR_AMPS_TIMEOUT_CYCLES (CHA_CYCLES_PER_SEC * 1) // 1 second

/// <summary>
/// get estimated battery volt from target and soc.
/// make a lot of assumtions:-)
/// maxVolt = target - 10
/// Based on Leaf data (soc from dash, volts from leafSpy).
/// Previous logic used soc and volts from leafSpy, but soc's in dash are completely different from those in leafSpy, and obviosly dash soc are sent in chademo.
/// </summary>
float GetEstimatedBatteryVoltage(float target, float soc)
{
    float maxVolt = target - 10;

    float nomVolt;
    if (target < 410f)
    {
        // Low segment: iMiev 370 -> 330 and meets high segment at 410
        nomVolt = 0.625f * target + 98.75f;
    }
    else
    {
        // High segment: Leaf 40: 410 -> 355, BMW i5 M60: 450 -> 400
        nomVolt = 1.125f * target - 106.25f;
    }

    float minVolt = nomVolt - (maxVolt - nomVolt);

    float deltaLow = 0.35f * (nomVolt - minVolt);
    float deltaHigh = 0.55f * (maxVolt - nomVolt);

    float volt20 = nomVolt - deltaLow;
    float volt80 = nomVolt + deltaHigh;

    if (soc < 50.0f)
    {
        return volt20 + ((soc - 20.0f) / 30.0f) * (nomVolt - volt20);
    }
    else if (soc < 80.0f)
    {
        return nomVolt + ((soc - 50.0f) / 30.0f) * (volt80 - nomVolt);
    }
    else
    {
        return volt80 + ((soc - 80.0f) / 20.0f) * (maxVolt - volt80);
    }
}

void ChademoCharger::HandlePendingCarMessages()
{
    static msg100 _msg100 = {};
    static msg101 _msg101 = {};
    static msg102 _msg102 = {};
    static msg110 _msg110 = {};
    static msg200 _msg200 = {};
    static msg201 _msg201 = {};

    if (_msg100_pending)
    {
        _msg100_pending = false;

        COMPARE_SET(_msg100.m.MinimumChargeCurrent, _msg100_isr.m.MinimumChargeCurrent, "[cha] 100.MinimumChargeCurrent changed %d -> %d\r\n");
        COMPARE_SET(_msg100.m.MaximumChargeCurrent, _msg100_isr.m.MaximumChargeCurrent, "[cha] 100.MaximumChargeCurrent changed %d -> %d\r\n");

        COMPARE_SET(_msg100.m.MinimumBatteryVoltage, _msg100_isr.m.MinimumBatteryVoltage, "[cha] 100.MinimumBatteryVoltage changed %d -> %d\r\n");
        COMPARE_SET(_msg100.m.MaximumBatteryVoltage, _msg100_isr.m.MaximumBatteryVoltage, "[cha] 100.MaximumBatteryVoltage changed %d -> %d\r\n");

        COMPARE_SET(_msg100.m.SocPercentConstant, _msg100_isr.m.SocPercentConstant, "[cha] 100.SocPercentConstant changed %d -> %d\r\n");
        
        COMPARE_SET(_msg100.m.Unused7, _msg100_isr.m.Unused7, "[cha] 100.Unused7 changed %d -> %d\r\n");

        _carData.MinimumChargeCurrent = _msg100.m.MinimumChargeCurrent;
        _carData.MaxBatteryVoltage = _msg100.m.MaximumBatteryVoltage;
    }
    if (_msg101_pending)
    {
        _msg101_pending = false;

        COMPARE_SET(_msg101.m.MaximumChargingTime10s, _msg101_isr.m.MaximumChargingTime10s, "[cha] 101.MaximumChargingTime10s changed %d -> %d\r\n");
        COMPARE_SET(_msg101.m.MaximumChargingTimeMinutes, _msg101_isr.m.MaximumChargingTimeMinutes, "[cha] 101.MaximumChargingTimeMinutes changed %d -> %d\r\n");
        COMPARE_SET(_msg101.m.EstimatedChargingTimeMinutes, _msg101_isr.m.EstimatedChargingTimeMinutes, "[cha] 101.EstimatedChargingTimeMins changed %d -> %d\r\n");
        COMPARE_SET(_msg101.m.BatteryCapacity, _msg101_isr.m.BatteryCapacity, "[cha] 101.BatteryCapacity changed %d -> %d\r\n");

        COMPARE_SET(_msg101.m.Unused0, _msg101_isr.m.Unused0, "[cha] 101.Unused0 changed %d -> %d\r\n");
        COMPARE_SET(_msg101.m.Unused4, _msg101_isr.m.Unused4, "[cha] 101.Unused4 changed %d -> %d\r\n");
        COMPARE_SET(_msg101.m.Unused7, _msg101_isr.m.Unused7, "[cha] 101.Unused7 changed %d -> %d\r\n");

        _carData.EstimatedChargingTimeMins = _msg101.m.EstimatedChargingTimeMinutes;

        if (_msg101.m.MaximumChargingTime10s == 0xff)
            _carData.MaxChargingTimeSec = _msg101.m.MaximumChargingTimeMinutes * 60;
        else
            _carData.MaxChargingTimeSec = _msg101.m.MaximumChargingTime10s * 10;

        // Unstable before switch(k).
        _carData.BatteryCapacityKwh = _msg101.m.BatteryCapacity * 0.1f;
    }
    if (_msg102_pending)
    {
        _msg102_pending = false;

        COMPARE_SET(_msg102.m.ProtocolNumber, _msg102_isr.m.ProtocolNumber, "[cha] 102.ProtocolNumber changed %d -> %d\r\n");
        COMPARE_SET(_msg102.m.TargetBatteryVoltage, _msg102_isr.m.TargetBatteryVoltage, "[cha] 102.TargetBatteryVoltage changed %d -> %d\r\n");
        COMPARE_SET(_msg102.m.ChargingCurrentRequest, _msg102_isr.m.ChargingCurrentRequest, "[cha] 102.ChargingCurrentRequest changed %d -> %d\r\n");
        COMPARE_SET(_msg102.m.Faults, _msg102_isr.m.Faults, "[cha] 102.Faults changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg102.m.Status, _msg102_isr.m.Status, "[cha] 102.Status changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg102.m.SocPercent, _msg102_isr.m.SocPercent, "[cha] 102.SocPercent changed %d -> %d\r\n");
        COMPARE_SET(_msg102.m.Unused7, _msg102_isr.m.Unused7, "[cha] 102.Unused7 changed %d -> %d\r\n");

        _carData.CyclesSinceCarLastAskingAmps = 0; // for timeout
        _carData.Faults = (CarFaults)_msg102.m.Faults;
        _carData.Status = (CarStatus)_msg102.m.Status;
        _carData.ProtocolNumber = _msg102.m.ProtocolNumber;

        if (_state == ChargerState::ChargingLoop && _msg102.m.TargetBatteryVoltage > _chargerData.AvailableOutputVoltage)
        {
            printf("[cha] Car asking (%d) for more than max (%d) volts. Stopping.\r\n", _msg102.m.TargetBatteryVoltage, _chargerData.AvailableOutputVoltage);
            set_flag(&_chargerData.Status, ChargerStatus::CHARGING_SYSTEM_ERROR); // let error handler deal with it
        }
        else
        {
            _carData.TargetBatteryVoltage = _msg102.m.TargetBatteryVoltage;
        }

        if (_state == ChargerState::ChargingLoop && _msg102.m.ChargingCurrentRequest > _chargerData.MaxAvailableOutputCurrent)
        {
            printf("[cha] Car asking (%d) for more than max (%d) amps. Stopping.\r\n", _msg102.m.ChargingCurrentRequest, _chargerData.MaxAvailableOutputCurrent);
            set_flag(&_chargerData.Status, ChargerStatus::CHARGING_SYSTEM_ERROR); // let error handler deal with it
        }
        else
        {
            _carData.AskingAmps = _msg102.m.ChargingCurrentRequest;
        }

        // soc and the constant both unstable before switch(k)
        if (_switch_k)
        {
            // TODO: verify that soc calculation works for chademo 0.9 cars

            if (_msg100.m.SocPercentConstant > 0 && _msg100.m.SocPercentConstant != 100)
                _carData.SocPercent = ((float)_msg102.m.SocPercent / _msg100.m.SocPercentConstant) * 100;
            else
                _carData.SocPercent = _msg102.m.SocPercent;

            if (_carData.SocPercent > 100)
            {
                // TODO: if this happens...maybe failing would be a better way to handle it...
                printf("[cha] Car report soc %d > 100. Failover to 100.\r\n", _carData.SocPercent);
                _carData.SocPercent = 100;
            }

            _carData.EstimatedBatteryVoltage = GetEstimatedBatteryVoltage(_carData.TargetBatteryVoltage, _carData.SocPercent);
        }

        _msg102_recieved = true;
    }
    if (_msg110_pending)
    {
        _msg110_pending = false;

        COMPARE_SET(_msg110.m.ExtendedFunction1, _msg110_isr.m.ExtendedFunction1, "[cha] 110.ExtendedFunction1 changed 0x%x -> 0x%x\r\n");

        ExtendedFunction1 extFun = (ExtendedFunction1)_msg110.m.ExtendedFunction1;
        _carData.SupportDynamicAvailableOutputCurrent = has_flag(extFun, ExtendedFunction1::DYNAMIC_CONTROL);
    }
    if (_msg200_pending)
    {
        _msg200_pending = false;

        COMPARE_SET(_msg200.m.MaximumDischargeCurrentInverted, _msg200_isr.m.MaximumDischargeCurrentInverted, "[cha] 200.MaximumDischargeCurrentInverted changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg200.m.Unused1, _msg200_isr.m.Unused1, "[cha] 200.Unused1 changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg200.m.Unused2, _msg200_isr.m.Unused2, "[cha] 200.Unused2 changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg200.m.Unused3, _msg200_isr.m.Unused3, "[cha] 200.Unused3 changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg200.m.MinimumDischargeVoltage, _msg200_isr.m.MinimumDischargeVoltage, "[cha] 200.MinimumDischargeVoltage changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg200.m.MinimumBatteryDischargeLevel, _msg200_isr.m.MinimumBatteryDischargeLevel, "[cha] 200.MinimumBatteryDischargeLevel changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg200.m.MaxRemainingCapacityForCharging, _msg200_isr.m.MaxRemainingCapacityForCharging, "[cha] 200.MaxRemainingCapacityForCharging changed 0x%x -> 0x%x\r\n");
    }
    if (_msg201_pending)
    {
        _msg201_pending = false;

        COMPARE_SET(_msg201.m.V2HchargeDischargeSequenceNum, _msg201_isr.m.V2HchargeDischargeSequenceNum, "[cha] 201.V2HchargeDischargeSequenceNum changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg201.m.ApproxDischargeCompletionTime, _msg201_isr.m.ApproxDischargeCompletionTime, "[cha] 201.ApproxDischargeCompletionTime changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg201.m.AvailableVehicleEnergy, _msg201_isr.m.AvailableVehicleEnergy, "[cha] 201.AvailableVehicleEnergy changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg201.m.Unused5, _msg201_isr.m.Unused5, "[cha] 201.Unused5 changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg201.m.Unused6, _msg201_isr.m.Unused6, "[cha] 201.Unused6 changed 0x%x -> 0x%x\r\n");
        COMPARE_SET(_msg201.m.Unused7, _msg201_isr.m.Unused7, "[cha] 201.Unused7 changed 0x%x -> 0x%x\r\n");
    }
}

bool ChademoCharger::IsDiscoveryCompleted()
{
    // if discovery, we go here when done
    return _state == ChargerState::PreStart_DiscoveryCompleted_WaitForPreChargeStart;
}

void ChademoCharger::Run()
{
    // HandlePendingMessages uses _switch_k
    COMPARE_SET(_switch_k, GetSwitchK(), "[cha] switch(k) changed %d -> %d\r\n");

    SetChargerDataFromCcsParams();
    HandlePendingCarMessages();
    RunStateMachine();
    SetCcsParamsFromCarData();
    SendChargerMessages();

    Log();
}

void ChademoCharger::SetCcsParamsFromCarData()
{
    // target +1 to silence warning in pev_sendCurrentDemandReq
    // TODO: use _carData.MaxBatteryVoltage? But what is the point? We always just ask for target voltage anyways...
    Param::SetInt(Param::MaxVoltage, _carData.TargetBatteryVoltage + 1);
    Param::SetInt(Param::soc, _carData.SocPercent);
    Param::SetInt(Param::BatteryVoltage, _carData.EstimatedBatteryVoltage);

    // Only ask ccs for amps in the charging loop, regardless of what the car says (hide that eg. iMiev is always asking for minimum 1A regardless)
    Param::SetInt(Param::ChargeCurrent, _state == ChargerState::ChargingLoop ? _carData.AskingAmps : 0);

    Param::SetInt(Param::TargetVoltage, _carData.TargetBatteryVoltage);
}

bool ChademoCharger::IsTimeoutSec(uint16_t sec)
{
    if (_cyclesInState > (sec * CHA_CYCLES_PER_SEC))
    {
        printf("[cha] Timeout in %s (max:%dsec)\r\n", GetStateName(), sec);
        return true;
    }
    return false;
}

// same as IsTimeoutSec, but without the logging
bool ChademoCharger::HasElapsedSec(uint16_t sec)
{
    return (_cyclesInState > (sec * CHA_CYCLES_PER_SEC));
}

void ChademoCharger::RunStateMachine()
{
    _cyclesInState++;

    if (_state < ChargerState::ChargingLoop)
    {
        StopReason stopReason = StopReason::NONE;
        // global reason
        if (_global.powerOffPending) set_flag(&stopReason, StopReason::POWER_OFF_PENDING);
        // car reason
        if (has_flag(_carData.Status, CarStatus::STOP_BEFORE_CHARGING)) set_flag(&stopReason, StopReason::CAR_STOP_BEFORE_CHARGING);
        if (has_flag(_carData.Status, CarStatus::ERROR)) set_flag(&stopReason, StopReason::CAR_ERROR);
        // charger reason
        if (has_flag(_chargerData.Status, ChargerStatus::CHARGER_ERROR)) set_flag(&stopReason, StopReason::CHARGER_ERROR);
        if (has_flag(_chargerData.Status, ChargerStatus::CHARGING_SYSTEM_ERROR)) set_flag(&stopReason, StopReason::CHARGING_SYSTEM_ERROR);
        if (has_flag(_chargerData.Status, ChargerStatus::BATTERY_INCOMPATIBLE)) set_flag(&stopReason, StopReason::BATTERY_INCOMPATIBLE);

        if (stopReason != StopReason::NONE)
        {
            printf("[cha] Stopping before starting\r\n");
            SetState(ChargerState::Stopping_Start, stopReason);
        }

        _chargerData.ThresholdVoltage = min(_chargerData.AvailableOutputVoltage, _carData.MaxBatteryVoltage);
        _chargerData.RemainingChargeTimeSec = _carData.MaxChargingTimeSec;
        _chargerData.RemainingChargeTimeCycles = _chargerData.RemainingChargeTimeSec * CHA_CYCLES_PER_SEC;
    }

    if (_state == ChargerState::PreStart_DiscoveryCompleted_WaitForPreChargeStart)
    {
        if (_global.ccsPreChargeStartedTrigger)
        {
            SetState(ChargerState::Start);
        }
    }
    else if (_state == ChargerState::Start)
    {
        // reset in case set during discovery
        _msg102_recieved = false;
        _carData.Faults = {};
        _carData.Status = {};
        _chargerData.Status = ChargerStatus::STOPPED;
        _stopReason = StopReason::NONE;

        SetSwitchD1(true); // will trigger car sending CAN

        SetState(ChargerState::WaitForCarReadyToCharge);
    }
    else if (_state == ChargerState::WaitForCarReadyToCharge)
    {
        if (_switch_k && has_flag(_carData.Status, CarStatus::READY_TO_CHARGE))
        {
            // Spec is confusing, but MinimumBatteryVoltage is ment to be used to judge incompatible battery (but only using target here),
            // and MinimumBatteryVoltage is unstable before switch(k), so indirectly, incompatible battery can not be judged before switch(k)
            if (_carData.TargetBatteryVoltage > _chargerData.AvailableOutputVoltage)
            {
                printf("[cha] car TargetBatteryVoltage %d > charger AvailableOutputVoltage %d (incompatible).\r\n", _carData.TargetBatteryVoltage, _chargerData.AvailableOutputVoltage);
                set_flag(&_chargerData.Status, ChargerStatus::BATTERY_INCOMPATIBLE); // let error handler deal with it
            }
            else if (_discovery)
            {
                SetSwitchD1(false); // PS: even if we set to false, car can continue to send 102 for a short time
                _discovery = false;
                printf("[cha] Discovery completed\r\n");

                SetState(ChargerState::PreStart_DiscoveryCompleted_WaitForPreChargeStart);
            }
            else
            {
                LockChargingPlug();
                set_flag(&_chargerData.Status, ChargerStatus::ENERGIZING_OR_PLUG_LOCKED);

                //PerformInsulationTest();

                SetState(ChargerState::WaitForPreChargeDone);
            }
        }
        else if (IsTimeoutSec(20))
        {
            SetState(ChargerState::Stopping_Start, StopReason::TIMEOUT);
        }
    }
    else if (_state == ChargerState::WaitForPreChargeDone)
    {
        if (_preChargeDoneButStalled)
        {
            SetSwitchD2(true);

            SetState(ChargerState::WaitForCarContactorsClosed);
        }
    }
    else if (_state == ChargerState::WaitForCarContactorsClosed)
    {
        if ((_carData.ProtocolNumber >= ProtocolNumber::Chademo_1_0 && has_flag(_carData.Status, CarStatus::CONTACTOR_OPEN_OR_WELDING_DETECTION_DONE) == false)
            // cha 0.9 does not use the flag (or it is unreliable?) so use askingamps as trigger
            || (_carData.ProtocolNumber == ProtocolNumber::Chademo_0_9 && _carData.AskingAmps > 0)
            // cha 0. iMiev ask for 1A from the start and won't ask for more until contactor is closed. It is suggested that iMiev need ~ 1 second after D2:true.
            || (_carData.ProtocolNumber == ProtocolNumber::Chademo_0 && HasElapsedSec(3)))
        {
            // Car seems to demand 0 volt on the wire when D2=true, else it wont close....at least not easily!!! This hack makes it work reliably.
            CloseAdapterContactor();

            SetState(ChargerState::WaitForCarAskingAmps);
        }
        else if (IsTimeoutSec(20))
        {
            SetState(ChargerState::Stopping_Start, StopReason::TIMEOUT);
        }
    }
    else if (_state == ChargerState::WaitForCarAskingAmps)
    {
        if (_carData.AskingAmps > 0)
        {
            // Even thou charger not delivering amps yet, we set these flags (seen in canlogs)
            set_flag(&_chargerData.Status, ChargerStatus::CHARGING);
            clear_flag(&_chargerData.Status, ChargerStatus::STOPPED);

            SetState(ChargerState::ChargingLoop);
        }
        else if (IsTimeoutSec(20))
        {
            SetState(ChargerState::Stopping_Start, StopReason::TIMEOUT);
        }
    }
    else if (_state == ChargerState::ChargingLoop)
    {
        if (_chargerData.RemainingChargeTimeCycles > 0)
            _chargerData.RemainingChargeTimeCycles--;
        _chargerData.RemainingChargeTimeSec = _chargerData.RemainingChargeTimeCycles / CHA_CYCLES_PER_SEC;

        StopReason stopReason = StopReason::NONE;
        // global reason
        if (_global.powerOffPending) set_flag(&stopReason, StopReason::POWER_OFF_PENDING);
        // car reasons
        if (_carData.CyclesSinceCarLastAskingAmps++ > LAST_ASKING_FOR_AMPS_TIMEOUT_CYCLES) set_flag(&stopReason, StopReason::CAR_CAN_AMPS_TIMEOUT);
        if (_switch_k == false) set_flag(&stopReason, StopReason::CAR_SWITCH_K_OFF);
        if (has_flag(_carData.Status, CarStatus::READY_TO_CHARGE) == false) set_flag(&stopReason, StopReason::CAR_NOT_READY_TO_CHARGE);
        if (has_flag(_carData.Status, CarStatus::NOT_IN_PARK)) set_flag(&stopReason, StopReason::CAR_NOT_IN_PARK);
        if (has_flag(_carData.Status, CarStatus::ERROR)) set_flag(&stopReason, StopReason::CAR_ERROR);
        // charger reasons
        if (has_flag(_chargerData.Status, ChargerStatus::CHARGING_SYSTEM_ERROR)) set_flag(&stopReason, StopReason::CHARGING_SYSTEM_ERROR);
        if (has_flag(_chargerData.Status, ChargerStatus::CHARGER_ERROR)) set_flag(&stopReason, StopReason::CHARGER_ERROR);
        if (_chargerData.RemainingChargeTimeSec == 0) set_flag(&stopReason, StopReason::CHARGING_TIME);

        // TODO: stop when battery full? No...normally the car will stop when it means it is full.

        if (stopReason != StopReason::NONE)
        {
            SetState(ChargerState::Stopping_Start, stopReason);
        }
    }
    else if (_state == ChargerState::Stopping_Start)
    {
        set_flag(&_chargerData.Status, ChargerStatus::STOPPED);

        SetState(ChargerState::Stopping_WaitForLowAmps);
    }
    else if (_state == ChargerState::Stopping_WaitForLowAmps)
    {
        if (_chargerData.OutputCurrent <= 5 || IsTimeoutSec(10))
        {
            clear_flag(&_chargerData.Status, ChargerStatus::CHARGING);

            SetState(ChargerState::Stopping_WaitForCarContactorsOpen);
        }
    }
    else if (_state == ChargerState::Stopping_WaitForCarContactorsOpen)
    {
        if (has_flag(_carData.Status, CarStatus::CONTACTOR_OPEN_OR_WELDING_DETECTION_DONE)
            || (_carData.ProtocolNumber < ProtocolNumber::Chademo_1_0 && HasElapsedSec(4)) // Spec: car should perform WD within 4 seconds after amps drops <= 5
            || IsTimeoutSec(10))
        {
            // welding detection done & car contactors open
            printf("[cha] Car contactors open\r\n");
            OpenAdapterContactor();
            SetSwitchD2(false);

            SetState(ChargerState::Stopping_WaitForLowVolts);
        }
    }
    else if (_state == ChargerState::Stopping_WaitForLowVolts)
    {
        // cha spec says <= 10 but we need to play by ccs rules here and at least some Tesla chargers never drop below 16 volts. Seen others never drop below 28.
        if (_chargerData.OutputVoltage <= 30 || IsTimeoutSec(10))
        {
            UnlockChargingPlug();
            clear_flag(&_chargerData.Status, ChargerStatus::ENERGIZING_OR_PLUG_LOCKED);

            // do stop CAN in own state to make sure we send this message to car before we kill CAN
            SetState(ChargerState::Stopping_End);
        }
    }
    else if (_state == ChargerState::Stopping_End)
    {
        // spec says, after setting D2:false wait 500ms before setting D1:false. We have passed 2 state changes already, so we have 300ms left.
        if (_cyclesInState > 3)
        {
            // this stops CAN
            SetSwitchD1(false);

            SetState(ChargerState::Stopped);
        }
    }
    else if (_state == ChargerState::Stopped)
    {
        if (_stopReason == StopReason::NONE)
        {
            printf("[cha] BUG: StopReason not set. Failover to UNKNOWN\r\n");
            _stopReason = StopReason::UNKNOWN;
        }
    }
}

bool ChademoCharger::PreChargeCompleted()
{
    _preChargeDoneButStalled = true;
    _global.ccsPreChargeDoneButStalledTrigger = true;

    // keep it hanging until car contactors closed
    bool carContactorsClosed = _state > ChargerState::WaitForCarContactorsClosed;
    if (!carContactorsClosed)
        printf("[cha] PreCharge stalled until car contactors closed\r\n");
    return carContactorsClosed;
}

extern "C" bool chademoInterface_preChargeCompleted()
{
    return chademoCharger->PreChargeCompleted();
}

bool ChademoCharger::ContinueWeldingDetection()
{
    bool contactorsOpened = _state > ChargerState::Stopping_WaitForCarContactorsOpen;
    // continue rebooting weldingDetection until car contactors are opened
    return contactorsOpened == false;
}

extern "C" bool chademoInterface_continueWeldingDetection()
{
    return chademoCharger->ContinueWeldingDetection();
}

void ChademoCharger::SetState(ChargerState newState, StopReason stopReason)
{
    printf("[cha] ====>>>> set state %s\r\n", _stateNames[newState]);
    _state = newState;
    _cyclesInState = 0;

    set_flag(&_stopReason, stopReason);
    if (_stopReason != StopReason::NONE)
        printf("[cha] Stopping: 0x%x\r\n", _stopReason);

    // force log on state change
    Log(true);
};

const char* ChademoCharger::GetStateName()
{
    return _stateNames[_state];
};

#define MAX_UNDERSUPPLY_AMPS 10

void ChademoCharger::SetChargerData(uint16_t maxV, uint16_t maxA, uint16_t outV, uint16_t outA)
{
    _chargerData.AvailableOutputVoltage = maxV;
    if (_chargerData.AvailableOutputVoltage > ADAPTER_MAX_VOLTS)
        _chargerData.AvailableOutputVoltage = ADAPTER_MAX_VOLTS;

    _chargerData.MaxAvailableOutputCurrent = clampToUint8(maxA);
    if (_chargerData.MaxAvailableOutputCurrent > ADAPTER_MAX_AMPS)
        _chargerData.MaxAvailableOutputCurrent = ADAPTER_MAX_AMPS;

    _chargerData.OutputVoltage = outV;

    _chargerData.OutputCurrent = clampToUint8(outA);

    // If difference between AskingAmps and OutputCurrent is too large, the car fails. If car support dynamic AvailableOutputCurrent,
    // we adjust AvailableOutputCurrent down, forcing the car to ask for less amps, reducing the difference.
    // Its kind of silly...why did they not provide a flag to turn off the car failing part instead? :-)
    // I don't know exactly what difference is allowed (spec. says 10% or 20A). At least 10A difference seems to work fine. 40A certainly does not:-)
    if (_carData.AskingAmps > _chargerData.OutputCurrent + MAX_UNDERSUPPLY_AMPS && _carData.SupportDynamicAvailableOutputCurrent)
        _chargerData.AvailableOutputCurrent = _chargerData.OutputCurrent + MAX_UNDERSUPPLY_AMPS;
    else
        _chargerData.AvailableOutputCurrent = _chargerData.MaxAvailableOutputCurrent;
};

void ChademoCharger::SetChargerDataFromCcsParams()
{
    if (_discovery)
    {
        // fake for discovery
        SetChargerData(ADAPTER_MAX_VOLTS, ADAPTER_MAX_AMPS, 0, 0);
    }
    else
    {
        // mirror these values
       SetChargerData(
            Param::GetInt(Param::EvseMaxVoltage),
            Param::GetInt(Param::EvseMaxCurrent),
            Param::GetInt(Param::EvseVoltage),
            Param::GetInt(Param::EvseCurrent)
        );
    }
}

void ChademoCharger::Log(bool force)
{
    if (force || _logCycleCounter++ > (CHA_CYCLES_PER_SEC * 1))
    {
        // every second or when forced
        printf("[cha] state:%s cycles:%d charger: out:%dV/%dA avail:%dV/%dA/%dA rem_t:%ds thres=%dV st=0x%x car: ask:%dA cap=%fkWh est_t:%dm err:0x%x max:%dV max_t:%ds min:%dA soc:%d%% st:0x%x pn:%d target:%dV batt:%dV\r\n",
            GetStateName(),
            _cyclesInState,
            _chargerData.OutputVoltage,
            _chargerData.OutputCurrent,
            _chargerData.AvailableOutputVoltage,
            _chargerData.MaxAvailableOutputCurrent,
            _chargerData.AvailableOutputCurrent,
            _chargerData.RemainingChargeTimeSec,
            _chargerData.ThresholdVoltage,
            _chargerData.Status,

            _carData.AskingAmps,
            &_carData.BatteryCapacityKwh,  // bypass float to double promotion by passing as reference
            _carData.EstimatedChargingTimeMins,
            _carData.Faults,
            _carData.MaxBatteryVoltage,
            _carData.MaxChargingTimeSec,
            _carData.MinimumChargeCurrent,
            _carData.SocPercent,
            _carData.Status,
            _carData.ProtocolNumber,
            _carData.TargetBatteryVoltage,
            _carData.EstimatedBatteryVoltage
        );

        _logCycleCounter = 0;
    }
}

void ChademoCharger::HandleCanMessageIsr(uint32_t id, uint32_t data[2])
{
    if (id == 0x100)
    {
        _msg100_pending = true;
        _msg100_isr.pair[0] = data[0];
        _msg100_isr.pair[1] = data[1];
    }
    else if (id == 0x101)
    {
        _msg101_pending = true;
        _msg101_isr.pair[0] = data[0];
        _msg101_isr.pair[1] = data[1];
    }
    else if (id == 0x102)
    {
        _msg102_pending = true;
        _msg102_isr.pair[0] = data[0];
        _msg102_isr.pair[1] = data[1];
    }
    else if (id == 0x110)
    {
        _msg110_pending = true;
        _msg110_isr.pair[0] = data[0];
        _msg110_isr.pair[1] = data[1];
    }
    else if (id == 0x200)
    {
        _msg200_pending = true;
        _msg200_isr.pair[0] = data[0];
        _msg200_isr.pair[1] = data[1];
    }
    else if (id == 0x201)
    {
        _msg201_pending = true;
        _msg201_isr.pair[0] = data[0];
        _msg201_isr.pair[1] = data[1];
    }
}


/* Interrupt service routines */
extern "C" void can1_rx0_isr(void)
{
    uint32_t id;
    bool ext, rtr;
    uint8_t length, fmi;
    uint32_t data[2];

    while (can_receive(CAN1,
        0, // fifo
        true,  // release
        &id,
        &ext,
        &rtr,
        &fmi, // ID of the matched filter
        &length,
        (uint8_t*)data,
        0 // timestamp
    ) > 0 && length == 8) // log if len is <> 8?
    {
        chademoCharger->HandleCanMessageIsr(id, data);
    }
}

// CAN: 100ms +-10ms
#define CAN_TRANSMIT_TIMEOUT_MS 10

/// <summary>
/// can_transmit is not fifo! It can choose avail. mailbox at will and send them in any order.
/// But chademo says 108 must come before 109, order of messages are defined.
/// So...wait until the message is send before q-ing next (can_transmit = add to mailbox q)
/// </summary>
void can_transmit_blocking_fifo(uint32_t canport, uint32_t id, bool ext, bool rtr, uint8_t len, uint8_t* data)
{
    // Check if CAN is initialized and not in bus-off state
    //if (CAN_MSR(canport) & CAN_MSR_INAK) {
    //    printf("[can] transmit: peripheral not initialized\r\n");
    //    return;// CAN_TX_ERROR;
    //}

    int mailbox = can_transmit(canport, id, ext, rtr, len, data);
    if (mailbox < 0) {
        printf("[cha] transmit: no mailbox available\r\n");
        return;// CAN_TX_NO_MAILBOX;
    }

    uint32_t txok_mask = CAN_TSR_TXOK0 << mailbox;
    uint32_t rqcp_mask = CAN_TSR_RQCP0 << mailbox;
//    uint32_t terr_mask = CAN_TSR_TERR0 << mailbox;

    // wait until done (TX done or error) or timeout
    uint32_t start = system_millis;
    while ((CAN_TSR(canport) & rqcp_mask) == 0) {
        if ((system_millis - start) > CAN_TRANSMIT_TIMEOUT_MS) {
            printf("[cha] transmit timeout for ID 0x%x\r\n", id);
            break;
        }
    }

    // Now safe to check success or error
    if ((CAN_TSR(canport) & txok_mask) != 0) {
        // success
    }
    else {
        // failure or abort
    }

    // Always clear request complete flag RQCPx
    CAN_TSR(canport) |= rqcp_mask;
}

void ChademoCharger::SendChargerMessages()
{
    if (_switch_d1 && _msg102_recieved)
    {
        UpdateChargerMessages();

        can_transmit_blocking_fifo(CAN1, 0x108, 0x108 > 0x7FF, false, 8, _msg108.bytes);
        can_transmit_blocking_fifo(CAN1, 0x109, 0x109 > 0x7FF, false, 8, _msg109.bytes);

        can_transmit_blocking_fifo(CAN1, 0x118, 0x118 > 0x7FF, false, 8, _msg118.bytes);

        // if charger discharge is enabled? and car discharge enabled. and discharge enabled here?
        if (_discharge)
        {
            can_transmit_blocking_fifo(CAN1, 0x208, 0x208 > 0x7FF, false, 8, _msg208.bytes);
            can_transmit_blocking_fifo(CAN1, 0x209, 0x209 > 0x7FF, false, 8, _msg209.bytes);
        }
    }
}


void ChademoCharger::UpdateChargerMessages()
{
    COMPARE_SET(_msg108.m.WeldingDetection, _chargerData.SupportWeldingDetection, "[cha] 108.WeldingDetection changed %d -> %d\r\n");
    COMPARE_SET(_msg108.m.AvailableOutputCurrent, _chargerData.AvailableOutputCurrent, "[cha] 108.AvailableOutputCurrent changed %d -> %d\r\n");
    COMPARE_SET(_msg108.m.AvailableOutputVoltage, _chargerData.AvailableOutputVoltage, "[cha] 108.AvailableOutputVoltage changed %d -> %d\r\n");
    COMPARE_SET(_msg108.m.ThresholdVoltage, _chargerData.ThresholdVoltage, "[cha] 108.ThresholdVoltage changed %d -> %d\r\n");

    COMPARE_SET(_msg109.m.ProtocolNumber, _chargerData.ProtocolNumber, "[cha] 109.ProtocolNumber changed %d -> %d\r\n");
    COMPARE_SET(_msg109.m.PresentChargingCurrent, _chargerData.OutputCurrent, "[cha] 109.OutputCurrent changed %d -> %d\r\n");

    // real outVolt after car contactors close. Before this, use the simulated volt (currently always 0).
    uint16_t outputVolt = _state > ChargerState::WaitForCarContactorsClosed ? _chargerData.OutputVoltage : 0;
    COMPARE_SET(_msg109.m.PresentVoltage, outputVolt, "[cha] 109.OutputVoltage changed %d -> %d\r\n");

    uint8_t remainingChargingTime10s = 0;
    uint8_t remainingChargingTimeMins = 0;
    if (_chargerData.RemainingChargeTimeSec < 60)
    {
        remainingChargingTime10s = _chargerData.RemainingChargeTimeSec / 10;
        remainingChargingTimeMins = 0;
    }
    else
    {
        remainingChargingTime10s = 0xff;
        remainingChargingTimeMins = _chargerData.RemainingChargeTimeSec / 60;
    }

    COMPARE_SET(_msg109.m.RemainingChargingTime10s, remainingChargingTime10s, "[cha] 109.RemainingChargingTime10s changed %d -> %d\r\n");
    COMPARE_SET(_msg109.m.RemainingChargingTimeMinutes, remainingChargingTimeMins, "[cha] 109.RemainingChargingTimeMinutes changed %d -> %d\r\n");
    COMPARE_SET(_msg109.m.Status, _chargerData.Status, "[cha] 109.Status changed 0x%x -> 0x%x\r\n");

    ExtendedFunction1 extFun = {};
    if (_chargerData.SupportDynamicAvailableOutputCurrent) set_flag(&extFun, ExtendedFunction1::DYNAMIC_CONTROL);
    COMPARE_SET(_msg118.m.ExtendedFunction1, extFun, "[cha] 118.ExtendedFunction1 changed 0x%x -> 0x%x\r\n");
}

bool ChademoCharger::IsPowerOffOk()
{
    // Must have time to tell the car via CAN that plug is unlocked, so auto off when UnlockChargingPlug() is a bit too soon.

    if (_state == ChargerState::Stopped) {
        // power off ok if stopped
        return true;
    }
    else {
        // not stopped (maybe not even started), then power off ok if plug was never locked
        return _chargingPlugLockedTrigger == false;
    }
}

