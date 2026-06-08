# Auto Calibration for a Disassemblable CNC

Auto calibration is especially useful for this machine because the mechanical setup can shift each time it is disassembled and rebuilt.

After reassembly, the controller should not assume the frame, rails, belts, and switches are in exactly the same positions as before. A calibration flow gives the machine a way to validate the current build before cutting.

## Why It Helps

Mechanical conditions that may change after reassembly:

- Rail position changes slightly
- Belt tension changes
- Gantry alignment changes
- Limit switch position changes
- Usable travel changes
- Axis squareness changes
- Encoder relationship to travel may need verification

## What Calibration Should Check

The calibration routine should verify:

- Each axis moves in the expected direction
- Each encoder counts correctly
- The min and max ends are detected
- Current usable travel is measured
- Limit switches are wired correctly
- The machine is assembled within tolerance

## Recommended Setup Flow

1. Assemble the machine.
2. Run **Calibrate Machine**.
3. Home each axis.
4. Measure travel from min to max.
5. Verify encoder counts.
6. Save the current travel and soft-limit values.
7. Report any axis that looks wrong.

## Notes

Use normally closed limit switches for each min and max input. Separate min/max inputs are preferred for calibration because the firmware can confirm the expected switch was hit for the commanded direction.

This calibration flow should be a setup and diagnostic feature, not a replacement for normal homing before jobs.
