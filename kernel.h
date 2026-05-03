#pragma once

// sbi: interface between kernel and firmware
struct sbiret {
    long error;
    long value;
};