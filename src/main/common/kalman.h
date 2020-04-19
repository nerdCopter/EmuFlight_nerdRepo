#pragma once

#include "sensors/gyro.h"
#include "filter.h"

#define MAX_KALMAN_WINDOW_SIZE 512

#define r_weight 0.67f

typedef struct variance
{
    float xVar;
    float yVar;
    float zVar;

    uint32_t windex;
    float xWindow[MAX_KALMAN_WINDOW_SIZE];
    float yWindow[MAX_KALMAN_WINDOW_SIZE];
    float zWindow[MAX_KALMAN_WINDOW_SIZE];

    float xSumMean;
    float ySumMean;
    float zSumMean;

    float xMean;
    float yMean;
    float zMean;

    float xSumVar;
    float ySumVar;
    float zSumVar;
    float xySumCoVar;
    float xzSumCoVar;
    float yzSumCoVar;

    float inverseN;
    uint16_t w;
} variance_t;

typedef struct kalman
{
    float q;     //process noise covariance
    float r;     //measurement noise covariance
    float p;     //estimation error covariance matrix
    float k;     //kalman gain
    float x;     //state
    float lastX; //previous state
    float e;
    float s;
} kalman_t;

extern void kalman_init(void);
extern void kalman_update(float* input, float* output);