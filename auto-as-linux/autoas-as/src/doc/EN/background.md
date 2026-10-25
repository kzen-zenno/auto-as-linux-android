---
layout: post
title: Introduction
category: AUTOSAR
comments: true
---

# What Am I Working On?

Since graduation, influenced by the open-source spirit of Linux, I have kept writing code and publishing it every day. Most of that work has been related to AUTOSAR and its toolchains. However, because of limited time and personal capacity, much of the development was based on ArcCore's open-source AUTOSAR release and published in [autoas/as](https://github.com/autoas/as).

Yes, I have now abandoned that repository, for several reasons:

* ArcCore stopped updating after releasing AUTOSAR 3.1.
* ArcCore is licensed under GPLv2, which makes commercial use unrealistic.
* [autoas/as](https://github.com/autoas/as) gradually became more like a personal learning playground and accumulated many things unrelated to AUTOSAR.
* The repository became too complex for beginners.
* Very few people are willing to participate in open-source AUTOSAR work.

# What Do I Want To Build?

So I want to build something simpler. I am not trying to create a huge all-in-one platform. Instead, I want to develop small AUTOSAR modules that can be used independently and that have high practical value.

Some people may say that this no longer looks like a complete AUTOSAR stack, but that is not the point. AUTOSAR itself already has the idea of implementation conformance classes (ICC), and in some scenarios these small and focused modules are actually more useful. For example, for an MCU bootloader, flash space is always tight, so the smaller the code size, the better. In that situation, standalone modules such as CANTP, LINTP, and DCM are especially attractive.

At the same time, I also want to build a simulation platform. With software simulation, these small modules can be developed and verified without hardware, and other people can learn or evaluate them as long as they have a computer.

At the time of writing, my other repository, [autoas/ssas-public](https://github.com/autoas/ssas-public), has already taken shape. It already includes CAN and LIN communication and diagnostic stacks, Ethernet stacks based on sockets such as DoIP and SOMEIP/SD, an NVM stack based on EEP/FLS, and complete upper/lower bootloader solutions for both CAN and LIN.

These modules are not perfect yet. For example, they may not be 100% MISRA-compliant or fully aligned with functional safety requirements. Still, they are fully capable of handling many straightforward application needs.

So I would like to use this series of documents as a simple introduction to the new repository: how to set up the simulation environment, what APIs each module provides, and how to integrate and use each module.

# About Commercial Use

In the end, people pursue open source for many reasons. I started with learning as the primary goal, and open source really helped me acquire a lot of skills. But now I also admit there is some practical motivation involved. Whether something is open or closed, you still hope to gain something from it, whether reputation or income.

I also feel that I have reached a bottleneck in day-to-day development. For most conventional software work on common platforms, delivery is mostly a matter of time. The work starts to feel repetitive, like turning the same wheel again and again. If work is already repetitive, I would rather spend my spare time organizing earlier research, continuously developing and validating these small modules, and writing useful documentation. That is another kind of training for myself.

I also hope these modules can bring at least some modest return. So if this repository or these documents help you, donations are welcome. If your company happens to need one of these modules or a bootloader solution from this repository, feel free to contact me. Everything is negotiable.

Contact: parai@foxmail.com