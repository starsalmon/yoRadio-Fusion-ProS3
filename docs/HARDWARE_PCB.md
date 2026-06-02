## Hardware: PCB + schematic (ProS3 fork)

This fork includes the first revision of a custom PCB and exports for manufacturing.

### Files in `docs/`

- **EasyEDA Pro project archive**: [`docs/hardware/easyeda/ProPrj_yoRadio-Fusion-ProS3_2026-06-03`](hardware/easyeda/ProPrj_yoRadio-Fusion-ProS3_2026-06-03)  
  Note: this file is a **ZIP container** (EasyEDA Pro “project” format). It contains:
  - `*.esch` (schematic)
  - `*.epcb` (PCB)
  - symbol/footprint libraries used by the project  
  If you want to inspect it outside EasyEDA, you can temporarily **copy/rename it to `*.zip`** and extract it.

- **Schematic PDF export** (kept at the top-level for easy viewing): [`yoRadio-Fusion-ProS3_Schematic1_2026-06-03.pdf`](yoRadio-Fusion-ProS3_Schematic1_2026-06-03.pdf)

- **Gerbers / drill / fab outputs**: [`docs/hardware/pcb/yoRadio-Fusion-ProS3_PCB1_2026-06-03.zip`](hardware/pcb/yoRadio-Fusion-ProS3_PCB1_2026-06-03.zip)  
  Includes standard Gerbers (top/bottom copper, silkscreen, soldermask), drill files, board outline, plus:
  - `FlyingProbeTesting.json`
  - `How-to-order-PCB.txt`

### Notes

- The ambient light sensor (BH1750) shares the ProS3 I2C bus with the MAX17048 battery gauge (GPIO8/9). See `myoptions.h` for the BH1750 enable + tuning knobs.
- The bi-amp DSP setup assumes 2x MAX98357 strapped to L/R while sharing the same I2S bus; see the main `README.md` for the firmware side and MQTT controls.

