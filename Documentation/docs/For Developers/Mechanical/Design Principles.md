# Design Principles

As this is an open-source project intended for easy deployment, maintenance, and use in a makerspace or similar environment, the design is informed by the resources and limitations thereof of these environments. 

When designing hardware for the MakeACS Core, the following principles should be kept in mind;

**Use consistent, standard hardware and generic off-the-shelf components where possible**

* Variability in hardware required for assembly should be minimized, and no specialized hardware or hardware requiring specialized tools should be used. 
* Unless otherwise required by design, the MakeACS Core uses M3 hardware as much as possible.
* Any off-the-shelf component should be used only if;
    * It is of a generic function and form, such that it can be replaced with one from another manufacturer easily
    * Its implementation is minimally influential on the design at-large, such that its replacement is not a major issue.

**Designs should be optimized for manufacturing and assembly with standard "maker" tools.**

* Most structure should be intended to be 3D printed, using FDM 3D printing, optimized for machines of a standard to poor tolerance.
* Minimize the use of custom metal or non-FDM plastic parts.
* Do not require tooling and equipment that is specialized or has a barrier to use where possible
    * For instance, use captive nuts where possible instead of heat-set inserts

**Design for maximum flexibility in usage and modification**

* This project has a lot of variants, we should try to genericize mechanical components to be as re-usable as possible across different hardware variants and potential implementations.