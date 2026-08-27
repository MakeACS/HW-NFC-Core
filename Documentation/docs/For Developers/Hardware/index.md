# Hardware Overview

!!! info
    This section refers to *electrical* hardware, for mechanical hardware, see [Mechanical](/docs/For%20Developers/Mechanical/).

MakeACS Core hardware is developed in [EasyEDA Pro](https://pro.easyeda.com/), version 3.2X or later (must support .eprj2 format). It is intended to be used in "Half Offline" mode or similar, to take advantage of the LCSC/EasyEDA components library. 

All hardware is in a single .eprj2 file, with each major revision being a board within the file, with the name starting with the version (i.e "V3.1.0 NFC Core") for alphabetical ordering.

On major release, an outjob is exported and saved for posterity under the hardware's version number. This includes:
* Gerber
* CPL
* BOM
* 3D STEP
