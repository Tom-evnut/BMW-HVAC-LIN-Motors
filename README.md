Reverse Engineering work carried out by me on the BMW HVAC actuators.

Actuators from a BMW E90 and BMW 5 F-series work with the same commands

Part Numbers:
BMW E90 - 41475520

Example Arduino code for a Teensy 4.0 with LIN bus transciever provided

## Bench findings (2026-10-17)

[findings.md](findings.md) explains the values in the motors' LIN frames: the status flags, the 15-bit relative
position, the speed byte, Set_Position and Lock/Unlock, and the ~2.7 s LIN-timeout fail-safe run. Each conclusion
comes with the frames and timestamps behind it.

- [LDF/BMW_HVAC_Flap_Motors.ldf](LDF/BMW_HVAC_Flap_Motors.ldf): LIN 1.3 description of all the frames, with
  named flag and speed values (loads in SavvyCAN and python ldfparser)
- [Bench captures](Bench%20captures): the bench sessions as SavvyCAN native CSV and decoded text
- [Tools/apgrun.cs](Tools/apgrun.cs): the Windows console program used to poll and command the motors through a
  Microchip APGDT001 LIN analyser

**Keep LIN traffic going while the motors are powered**, power-up included: after about 2.7 s without bus traffic
every motor drives on its own towards one end of its range. Set_Position stops it.
