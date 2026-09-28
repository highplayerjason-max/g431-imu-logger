/**
  ******************************************************************************
  * @file    mahony.c
  * @brief   Mahony complementary filter (accel + gyro), based on the common
  *          MahonyAHRSupdateIMU formulation used by the module reference code.
  ******************************************************************************
  */

#include <math.h>

#include "mahony.h"

#define RAD_TO_DEG   (180.0f / 3.14159265358979323846f)

static float inv_sqrt(float x)
{
  return 1.0f / sqrtf(x);
}

void Mahony_Init(Mahony_t *f, float kp, float ki)
{
  f->q0 = 1.0f;
  f->q1 = 0.0f;
  f->q2 = 0.0f;
  f->q3 = 0.0f;

  f->twoKp = 2.0f * kp;
  f->twoKi = 2.0f * ki;

  f->integralFBx = 0.0f;
  f->integralFBy = 0.0f;
  f->integralFBz = 0.0f;

  f->roll  = 0.0f;
  f->pitch = 0.0f;
  f->yaw   = 0.0f;
}

void Mahony_Update(Mahony_t *f, float gx, float gy, float gz,
                   float ax, float ay, float az, float dt)
{
  float recipNorm;
  float halfvx, halfvy, halfvz;
  float halfex, halfey, halfez;
  float qa, qb, qc;

  const float q0 = f->q0;
  const float q1 = f->q1;
  const float q2 = f->q2;
  const float q3 = f->q3;

  /* Accelerometer gives the reference direction of gravity. */
  const float acc_norm = sqrtf(ax * ax + ay * ay + az * az);
  if (acc_norm > 1.0e-6f)
  {
    recipNorm = inv_sqrt(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    /* Estimated direction of gravity from the quaternion. */
    halfvx = q1 * q3 - q0 * q2;
    halfvy = q0 * q1 + q2 * q3;
    halfvz = q0 * q0 - 0.5f + q3 * q3;

    /* Error = measured cross estimated direction. */
    halfex = (ay * halfvz - az * halfvy);
    halfey = (az * halfvx - ax * halfvz);
    halfez = (ax * halfvy - ay * halfvx);

    /* Integral part of the PI controller. */
    if (f->twoKi > 0.0f)
    {
      f->integralFBx += f->twoKi * halfex * dt;
      f->integralFBy += f->twoKi * halfey * dt;
      f->integralFBz += f->twoKi * halfez * dt;

      gx += f->integralFBx;
      gy += f->integralFBy;
      gz += f->integralFBz;
    }

    gx += f->twoKp * halfex;
    gy += f->twoKp * halfey;
    gz += f->twoKp * halfez;
  }

  /* Integrate quaternion rate. */
  gx *= 0.5f * dt;
  gy *= 0.5f * dt;
  gz *= 0.5f * dt;
  qa = q0;
  qb = q1;
  qc = q2;

  f->q0 += (-qb * gx - qc * gy - q3 * gz);
  f->q1 += (qa * gx + qc * gz - q3 * gy);
  f->q2 += (qa * gy - qb * gz + q3 * gx);
  f->q3 += (qa * gz + qb * gy - qc * gx);

  /* Normalise quaternion. */
  recipNorm = inv_sqrt(f->q0 * f->q0 + f->q1 * f->q1 +
                       f->q2 * f->q2 + f->q3 * f->q3);
  f->q0 *= recipNorm;
  f->q1 *= recipNorm;
  f->q2 *= recipNorm;
  f->q3 *= recipNorm;

  /* Euler angles (degrees). Yaw is relative and will drift without magnetometer. */
  const float q0q1 = f->q0 * f->q1;
  const float q0q2 = f->q0 * f->q2;
  const float q0q3 = f->q0 * f->q3;
  const float q1q2 = f->q1 * f->q2;
  const float q1q3 = f->q1 * f->q3;
  const float q2q2 = f->q2 * f->q2;
  const float q2q3 = f->q2 * f->q3;
  const float q3q3 = f->q3 * f->q3;

  f->roll  = atan2f(2.0f * (q0q1 + q2q3), 1.0f - 2.0f * (f->q1 * f->q1 + q2q2)) * RAD_TO_DEG;
  f->pitch = asinf(2.0f * (q0q2 - q1q3)) * RAD_TO_DEG;
  f->yaw   = atan2f(2.0f * (q0q3 + q1q2), 1.0f - 2.0f * (q2q2 + q3q3)) * RAD_TO_DEG;
}
