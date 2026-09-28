/**
  ******************************************************************************
  * @file    mahony.h
  * @brief   Mahony complementary filter (accel + gyro, no magnetometer).
  ******************************************************************************
  */

#ifndef MAHONY_H
#define MAHONY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
  /* Quaternion (world <- body convention, q0 scalar) */
  volatile float q0;
  volatile float q1;
  volatile float q2;
  volatile float q3;

  /* Filter gains */
  float twoKp;
  float twoKi;
  float integralFBx;
  float integralFBy;
  float integralFBz;

  /* Euler output in degrees */
  volatile float roll;
  volatile float pitch;
  volatile float yaw;
} Mahony_t;

void Mahony_Init(Mahony_t *f, float kp, float ki);

/**
  * @param gx gy gz angular rate in rad/s
  * @param ax ay az acceleration in g
  * @param dt  seconds since last update
  */
void Mahony_Update(Mahony_t *f, float gx, float gy, float gz,
                   float ax, float ay, float az, float dt);

#ifdef __cplusplus
}
#endif

#endif /* MAHONY_H */
