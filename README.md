## Table of Contents

* [Navigating the Repository](#navigating-the-repository)
* [Change Log](#change-log)
* [Installation](#installation)
* [Required Tools](#required-tools)
* [Examples List](#examples--demo-list)
* [References](#references)
* [FAQ](docs/faq.md)
* [Versioning Proprietary Projects](docs/suggested_workflow.md)

# Introduction

These examples and demos are for **TI SimpleLink CC13X2 / CC26X2 SDK 3.30.00**.

This repository contains proprietary radio sample applications for Texas
Instruments' SimpleLink CC13X2 / CC26X2 SDK. These examples have not been
validated as production-ready.

**Do not** use GitHub's bug tracking feature for support. For inquiries, see the
forum relevant to your device and application:

* [Sub-1 Ghz Forum][E2E-SUB-1GHZ]
* [Other Wireless Forum][E2E-OTHER-WIRELESS]

To use the examples and tools in this repository, please download and install
the [SimpleLink CC13X2 / CC26X2 SDK][CC13X2-CC26X2-SDK] **first**, and if
necessary [buy an evaluation kit][CC1352R1-LP].

If you have any questions please refer to the [FAQ page](docs/faq.md).

For other SDK versions and platforms, refer to the table below.

| SDK                            | Version  | Tag                                                    |
| ------------------------------ | -------- | ------------------------------------------------------ |
| SimpleLink CC13x2 / CC26x2 SDK | 3.30.00  | [`simplelink_cc13x2_26x2_sdk-3.30.00.00`][TAG-3.30.00] |
| SimpleLink CC13x2 / CC26x2 SDK | 3.10.00  | [`simplelink_cc13x2_26x2_sdk-3.10.00.00`][TAG-3.10.00] |
| SimpleLink CC13x2 / CC26x2 SDK | 2.40.00  | [`simplelink_cc13x2_26x2_sdk-2.40.00.00`][TAG-2.40.00] |

[TAG-3.30.00]: https://github.com/ti-simplelink/proprietary_examples/tree/simplelink_cc13x2_26x2_sdk-3.30.00.00
[TAG-3.10.00]: https://github.com/ti-simplelink/proprietary_examples/tree/simplelink_cc13x2_26x2_sdk-3.10.00.00
[TAG-2.40.00]: https://github.com/ti-simplelink/proprietary_examples/tree/simplelink_cc13x2_26x2_sdk-2.40.00.00


# Navigating the Repository

The examples provided on this GitHub page serve as a plugin to a corresponding
device SDK release. The master branch will always point to the latest release.

Older releases can be accessed by checking out/downloading their corresponding
branch. For more information on supported examples please consult the README.md
of the desired branch/release.

For more information about different SDK components, please consult the SDK
documentation.


## Change Log

Note: The version numbers below are related to GitHub proprietary_examples
releases. The numbering scheme is in the form of M.mm.pp.bb. The fields pp.bb
are incremented as GitHub examples are released, M.mm will map a GitHub release
to a SimpleLink SDK release.

### 3.30.00

Ported examples from SDK version 3.10.00 to 3.30.00:
* RF Sensor and Collector


### 3.10.00

Ported examples from SDK version 2.40.00 to 3.10.00:
* RF Sensor and Collector


## Installation

This repository can be cloned/download anywhere on your computer. There is a
dependency between this repository and the [SimpleLink CC13x2 / CC26x2
SDK][CC13X2-CC26X2-SDK] install location.

By default the SDK will install to:

    C:\ti\simplelink_cc13x2_26x2_sdk_{version}

If the SDK must be installed to a different location, then see the [FAQ
page](docs/faq.md) for IDE specific instructions for changing environment
variables.


## Required Tools

The examples in this repository currently only support the CCS toolchains.
Please refer to the release notes for the supported versions of each toolchain.
Using a non-supported version is untested and may result in unexpected behavior.

For more information on toolchain setup, please refer to our [FAQ
page](docs/faq.md).


## FAQ

The [FAQ page](docs/faq.md) will try to address some of the common questions
related to the proprietary_examples repo.


## Examples / Demo List

### RF Sensor and Collector

A synchronous RF Sensor and Collector implementation using a proprietary
time-slotted radio protocol.

* rfSensor
    * [README](examples/rtos/CC1352R1_LAUNCHXL/rfSensor/README.md)
    * [CCS Project](examples/rtos/CC1352R1_LAUNCHXL/rfSensor/tirtos/ccs)
    * [Source](examples/rtos/CC1352R1_LAUNCHXL/rfSensor)


* rfCollector
    * [README](examples/rtos/CC1352R1_LAUNCHXL/rfCollector/README.md)
    * [CCS Project](examples/rtos/CC1352R1_LAUNCHXL/rfCollector/tirtos/ccs)
    * [Source](examples/rtos/CC1352R1_LAUNCHXL/rfCollector)


## References

The following reference pages may be helpful during general development using
the TI SimpleLink SDK. Users developing proprietary RF applications are
encouraged to read the [Proprietary RF User's Guide][PROP-RF-UG].

As an additional resource, users are encouraged to complete the [SimpleLink
Academy][SLA] training.

[E2E-SUB-1GHZ]: https://e2e.ti.com/support/wireless-connectivity/sub-1-ghz/f/156
[E2E-OTHER-WIRELESS]: https://e2e.ti.com/support/wireless-connectivity/other-wireless/f/667
[CC13X2-CC26X2-SDK]: http://www.ti.com/tool/SIMPLELINK_CC13X2_26X2_SDK
[CC1352R1-LP]: http://www.ti.com/tool/LAUNCHXL-CC1352R1
[PROP-RF-UG]: http://dev.ti.com/tirex/explore/node?node=AOFmdaK8impnxmEqGU3jkg__pTTHBmu__LATEST
[SLA]: http://dev.ti.com/tirex/explore/node?node=AHjJ8SNDuLXt3h6qHE4-OA__pTTHBmu__LATEST
