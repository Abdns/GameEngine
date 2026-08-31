#ifndef ENGINEMATH_H
#define ENGINEMATH_H

#include "Types.h"
#include "Intrinsics.h"

// =============================================================================
// Scalar
// =============================================================================

#define REAL32_LARGE 1.0e30f

#define Square(x)     ((x) * (x))
#define Minimum(a, b) ((a) < (b) ? (a) : (b))
#define Maximum(a, b) ((a) > (b) ? (a) : (b))

inline real32 DegToRad(real32 Degrees)
{
    return Degrees * (Pi32 / 180.0f);
}

inline real32 Lerp(real32 A, real32 t, real32 B)
{
    return (1.0f - t) * A + t * B;
}

inline real32 Clamp(real32 Min, real32 Value, real32 Max)
{
    real32 Result = Value;

    if (Result < Min)
    {
        Result = Min;
    }

    if (Result > Max)
    {
        Result = Max;
    }

    return Result;
}

inline real32 Clamp01(real32 Value)
{
    return Clamp(0.0f, Value, 1.0f);
}

// =============================================================================
// Vector2
// =============================================================================

union Vector2
{
    struct { real32 X, Y; };
    real32 Elements[2];

    Vector2() = default;
    Vector2(real32 InX, real32 InY) { X = InX; Y = InY; }
};

inline Vector2 operator+(Vector2 A, Vector2 B)
{
    return Vector2(A.X + B.X, A.Y + B.Y);
}

inline Vector2 operator-(Vector2 A, Vector2 B)
{
    return Vector2(A.X - B.X, A.Y - B.Y);
}

inline Vector2 operator*(real32 S, Vector2 A)
{
    return Vector2(S * A.X, S * A.Y);
}

inline Vector2 operator*(Vector2 A, real32 S)
{
    return Vector2(S * A.X, S * A.Y);
}

inline Vector2 &operator+=(Vector2 &A, Vector2 B)
{
    A = A + B;

    return A;
}

inline Vector2 &operator-=(Vector2 &A, Vector2 B)
{
    A = A - B;

    return A;
}

// =============================================================================
// Vector3
// =============================================================================

union Vector3
{
    struct { real32 X, Y, Z; };
    struct { Vector2 XY; real32 Z_; };
    real32 Elements[3];

    Vector3() = default;
    Vector3(real32 InX, real32 InY, real32 InZ) { X = InX; Y = InY; Z = InZ; }
};

inline Vector3 operator+(Vector3 A, Vector3 B)
{
    return Vector3(A.X + B.X, A.Y + B.Y, A.Z + B.Z);
}

inline Vector3 operator-(Vector3 A, Vector3 B)
{
    return Vector3(A.X - B.X, A.Y - B.Y, A.Z - B.Z);
}

inline Vector3 operator*(real32 S, Vector3 A)
{
    return Vector3(S * A.X, S * A.Y, S * A.Z);
}

inline Vector3 operator*(Vector3 A, real32 S)
{
    return Vector3(S * A.X, S * A.Y, S * A.Z);
}

inline Vector3 &operator+=(Vector3 &A, Vector3 B)
{
    A = A + B;

    return A;
}

inline Vector3 &operator-=(Vector3 &A, Vector3 B)
{
    A = A - B;

    return A;
}

inline real32 Dot(Vector3 A, Vector3 B)
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

inline Vector3 Cross(Vector3 A, Vector3 B)
{
    return Vector3(A.Y * B.Z - A.Z * B.Y,
                   A.Z * B.X - A.X * B.Z,
                   A.X * B.Y - A.Y * B.X);
}

inline real32 LengthSq(Vector3 A)
{
    return Dot(A, A);
}

inline real32 Length(Vector3 A)
{
    return SquareRoot(Dot(A, A));
}

inline Vector3 Normalize(Vector3 A)
{
    real32 Len = Length(A);
    if (Len > 0.0f)
    {
        return (1.0f / Len) * A;
    }

    return A;
}

// =============================================================================
// Vector4
// =============================================================================

union Vector4
{
    struct { real32 X, Y, Z, W; };
    struct { Vector3 XYZ; real32 W_; };
    real32 Elements[4];

    Vector4() = default;
    Vector4(real32 InX, real32 InY, real32 InZ, real32 InW) { X = InX; Y = InY; Z = InZ; W = InW; }
};

// =============================================================================
// Quaternion
// =============================================================================

union Quaternion
{
    struct { real32 X, Y, Z, W; };
    struct { Vector3 XYZ; real32 W_; };
    real32 Elements[4];

    Quaternion() = default;
    Quaternion(real32 InX, real32 InY, real32 InZ, real32 InW) { X = InX; Y = InY; Z = InZ; W = InW; }
};

inline Quaternion QuatIdentity(void)
{
    return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
}

inline real32 QuatDot(Quaternion A, Quaternion B)
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
}

inline Quaternion QuatNormalize(Quaternion Q)
{
    real32 LengthSquared = QuatDot(Q, Q);
    if (LengthSquared < Epsilon32)
    {
        return QuatIdentity();
    }

    real32 InvLength = 1.0f / SquareRoot(LengthSquared);

    return Quaternion(Q.X * InvLength, Q.Y * InvLength, Q.Z * InvLength, Q.W * InvLength);
}

inline Quaternion QuatConjugate(Quaternion Q)
{
    return Quaternion(-Q.X, -Q.Y, -Q.Z, Q.W);
}

inline Quaternion QuatMultiply(Quaternion A, Quaternion B)
{
    return Quaternion(A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y,
                      A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X,
                      A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W,
                      A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z);
}

inline Quaternion QuatFromAxisAngle(Vector3 Axis, real32 Angle)
{
    real32  HalfAngle = 0.5f * Angle;
    real32  SinHalf   = Sin(HalfAngle);
    Vector3 Normal    = Normalize(Axis);

    return Quaternion(Normal.X * SinHalf, Normal.Y * SinHalf, Normal.Z * SinHalf, Cos(HalfAngle));
}

inline Vector3 QuatRotate(Quaternion Q, Vector3 V)
{
    Vector3 Axis  = Vector3(Q.X, Q.Y, Q.Z);
    Vector3 Twice = 2.0f * Cross(Axis, V);

    return V + Q.W * Twice + Cross(Axis, Twice);
}

inline Vector3 QuatInverseRotate(Quaternion Q, Vector3 V)
{
    return QuatRotate(QuatConjugate(Q), V);
}

inline Quaternion QuatIntegrate(Quaternion Q, Vector3 AngularVelocity, real32 dt)
{
    Quaternion Spin  = Quaternion(AngularVelocity.X, AngularVelocity.Y, AngularVelocity.Z, 0.0f);
    Quaternion Delta = QuatMultiply(Spin, Q);
    real32     Half  = 0.5f * dt;

    return QuatNormalize(Quaternion(Q.X + Half * Delta.X,
                                    Q.Y + Half * Delta.Y,
                                    Q.Z + Half * Delta.Z,
                                    Q.W + Half * Delta.W));
}

inline Quaternion QuatNLerp(Quaternion A, Quaternion B, real32 T)
{
    real32 Sign = (QuatDot(A, B) < 0.0f) ? -1.0f : 1.0f;

    return QuatNormalize(Quaternion(A.X + T * (Sign * B.X - A.X),
                                    A.Y + T * (Sign * B.Y - A.Y),
                                    A.Z + T * (Sign * B.Z - A.Z),
                                    A.W + T * (Sign * B.W - A.W)));
}

// =============================================================================
// Matrix4
// =============================================================================

struct Matrix4
{
    real32 Elements[4][4];
};

inline Matrix4 Mat4Identity(void)
{
    Matrix4 Result = {};
    Result.Elements[0][0] = 1.0f;
    Result.Elements[1][1] = 1.0f;
    Result.Elements[2][2] = 1.0f;
    Result.Elements[3][3] = 1.0f;

    return Result;
}

inline Matrix4 Mat4Multiply(Matrix4 A, Matrix4 B)
{
    Matrix4 Result = {};
    for (int Column = 0; Column < 4; ++Column)
    {
        for (int Row = 0; Row < 4; ++Row)
        {
            real32 Sum = 0.0f;
            for (int Inner = 0; Inner < 4; ++Inner)
            {
                Sum += A.Elements[Inner][Row] * B.Elements[Column][Inner];
            }
            Result.Elements[Column][Row] = Sum;
        }
    }

    return Result;
}

inline Matrix4 Mat4Translation(real32 X, real32 Y, real32 Z)
{
    Matrix4 Result = Mat4Identity();
    Result.Elements[3][0] = X;
    Result.Elements[3][1] = Y;
    Result.Elements[3][2] = Z;

    return Result;
}

inline Matrix4 Mat4Scale(Vector3 Scale)
{
    Matrix4 Result = Mat4Identity();
    Result.Elements[0][0] = Scale.X;
    Result.Elements[1][1] = Scale.Y;
    Result.Elements[2][2] = Scale.Z;

    return Result;
}

inline Matrix4 Mat4RotationX(real32 Angle)
{
    real32 Cosine = Cos(Angle);
    real32 Sine   = Sin(Angle);
    Matrix4 Result = Mat4Identity();
    Result.Elements[1][1] =  Cosine;
    Result.Elements[1][2] =  Sine;
    Result.Elements[2][1] = -Sine;
    Result.Elements[2][2] =  Cosine;

    return Result;
}

inline Matrix4 Mat4RotationY(real32 Angle)
{
    real32 Cosine = Cos(Angle);
    real32 Sine   = Sin(Angle);
    Matrix4 Result = Mat4Identity();
    Result.Elements[0][0] =  Cosine;
    Result.Elements[0][2] = -Sine;
    Result.Elements[2][0] =  Sine;
    Result.Elements[2][2] =  Cosine;

    return Result;
}

inline Matrix4 Mat4RotationZ(real32 Angle)
{
    real32 Cosine = Cos(Angle);
    real32 Sine   = Sin(Angle);
    Matrix4 Result = Mat4Identity();
    Result.Elements[0][0] =  Cosine;
    Result.Elements[0][1] =  Sine;
    Result.Elements[1][0] = -Sine;
    Result.Elements[1][1] =  Cosine;

    return Result;
}

inline Matrix4 Mat4Perspective(real32 FovYRadians, real32 Aspect, real32 Near, real32 Far)
{
    real32 TanHalf = tanf(FovYRadians * 0.5f);
    Matrix4 Result = {};
    Result.Elements[0][0] = 1.0f / (Aspect * TanHalf);
    Result.Elements[1][1] = -1.0f / TanHalf;
    Result.Elements[2][2] = Far / (Near - Far);
    Result.Elements[2][3] = -1.0f;
    Result.Elements[3][2] = (Far * Near) / (Near - Far);

    return Result;
}

inline Matrix4 Mat4InverseRigid(Matrix4 M)
{
    Matrix4 Result = Mat4Identity();

    for (int Column = 0; Column < 3; ++Column)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            Result.Elements[Column][Row] = M.Elements[Row][Column];
        }
    }

    real32 Tx = M.Elements[3][0];
    real32 Ty = M.Elements[3][1];
    real32 Tz = M.Elements[3][2];

    Result.Elements[3][0] = -(Result.Elements[0][0] * Tx + Result.Elements[1][0] * Ty + Result.Elements[2][0] * Tz);
    Result.Elements[3][1] = -(Result.Elements[0][1] * Tx + Result.Elements[1][1] * Ty + Result.Elements[2][1] * Tz);
    Result.Elements[3][2] = -(Result.Elements[0][2] * Tx + Result.Elements[1][2] * Ty + Result.Elements[2][2] * Tz);

    return Result;
}

inline Matrix4 Mat4FromQuaternion(Quaternion Q)
{
    real32 X2 = Q.X + Q.X;
    real32 Y2 = Q.Y + Q.Y;
    real32 Z2 = Q.Z + Q.Z;

    real32 XX = Q.X * X2, XY = Q.X * Y2, XZ = Q.X * Z2;
    real32 YY = Q.Y * Y2, YZ = Q.Y * Z2, ZZ = Q.Z * Z2;
    real32 WX = Q.W * X2, WY = Q.W * Y2, WZ = Q.W * Z2;

    Matrix4 Result = {};

    Result.Elements[0][0] = 1.0f - (YY + ZZ);
    Result.Elements[0][1] = XY + WZ;
    Result.Elements[0][2] = XZ - WY;

    Result.Elements[1][0] = XY - WZ;
    Result.Elements[1][1] = 1.0f - (XX + ZZ);
    Result.Elements[1][2] = YZ + WX;

    Result.Elements[2][0] = XZ + WY;
    Result.Elements[2][1] = YZ - WX;
    Result.Elements[2][2] = 1.0f - (XX + YY);

    Result.Elements[3][3] = 1.0f;

    return Result;
}

inline Matrix4 Mat4Rigid(Vector3 Position, Quaternion Orientation)
{
    Matrix4 Result = Mat4FromQuaternion(Orientation);

    Result.Elements[3][0] = Position.X;
    Result.Elements[3][1] = Position.Y;
    Result.Elements[3][2] = Position.Z;

    return Result;
}

inline Vector3 Mat4Transform(Matrix4 M, Vector3 V, real32 W)
{
    return Vector3(M.Elements[0][0] * V.X + M.Elements[1][0] * V.Y + M.Elements[2][0] * V.Z + M.Elements[3][0] * W,
                   M.Elements[0][1] * V.X + M.Elements[1][1] * V.Y + M.Elements[2][1] * V.Z + M.Elements[3][1] * W,
                   M.Elements[0][2] * V.X + M.Elements[1][2] * V.Y + M.Elements[2][2] * V.Z + M.Elements[3][2] * W);
}

inline Vector3 Mat4Column(Matrix4 M, int Column)
{
    return Vector3(M.Elements[Column][0], M.Elements[Column][1], M.Elements[Column][2]);
}

inline void Mat4SetColumn(Matrix4 *M, int Column, Vector3 V)
{
    M->Elements[Column][0] = V.X;
    M->Elements[Column][1] = V.Y;
    M->Elements[Column][2] = V.Z;
}

// =============================================================================
// Transform
// =============================================================================

struct transform
{
    Vector3    Position;
    Quaternion Orientation;
    Vector3    Scale;
};

inline transform TransformIdentity(void)
{
    transform Result;
    Result.Position    = Vector3(0.0f, 0.0f, 0.0f);
    Result.Orientation = QuatIdentity();
    Result.Scale       = Vector3(1.0f, 1.0f, 1.0f);

    return Result;
}

inline transform TransformAt(Vector3 Position)
{
    transform Result = TransformIdentity();
    Result.Position = Position;

    return Result;
}

inline transform TransformLerp(transform A, transform B, real32 T)
{
    transform Result;
    Result.Position    = A.Position + T * (B.Position - A.Position);
    Result.Orientation = QuatNLerp(A.Orientation, B.Orientation, T);
    Result.Scale       = A.Scale + T * (B.Scale - A.Scale);

    return Result;
}

inline Matrix4 Mat4FromTransform(transform T)
{
    return Mat4Multiply(Mat4Rigid(T.Position, T.Orientation), Mat4Scale(T.Scale));
}

// =============================================================================
// Rectangle3
// =============================================================================

struct rectangle3
{
    Vector3 Min;
    Vector3 Max;
};

inline rectangle3 Rect3MinMax(Vector3 Min, Vector3 Max)
{
    rectangle3 Result;
    Result.Min = Min;
    Result.Max = Max;

    return Result;
}

inline rectangle3 Rect3CenterHalfDim(Vector3 Center, Vector3 HalfDim)
{
    return Rect3MinMax(Center - HalfDim, Center + HalfDim);
}

inline rectangle3 Rect3CenterRadius(Vector3 Center, real32 Radius)
{
    return Rect3CenterHalfDim(Center, Vector3(Radius, Radius, Radius));
}

inline rectangle3 Rect3AddRadius(rectangle3 Rect, real32 Radius)
{
    Vector3 R = Vector3(Radius, Radius, Radius);

    return Rect3MinMax(Rect.Min - R, Rect.Max + R);
}

inline Vector3 Rect3Center(rectangle3 Rect)
{
    return 0.5f * (Rect.Min + Rect.Max);
}

inline bool32 Rect3Contains(rectangle3 Rect, Vector3 P)
{
    return (P.X >= Rect.Min.X && P.X <= Rect.Max.X &&
            P.Y >= Rect.Min.Y && P.Y <= Rect.Max.Y &&
            P.Z >= Rect.Min.Z && P.Z <= Rect.Max.Z);
}

// =============================================================================
// Ray
// =============================================================================

struct ray
{
    Vector3 Origin;
    Vector3 Direction;
};

inline ray RayFromScreen(real32 MouseX, real32 MouseY, real32 ViewportWidth, real32 ViewportHeight, real32 FovYRadians, Vector3 CameraPosition, Vector3 CameraRight, Vector3 CameraUp, Vector3 CameraForward)
{
    real32 NdcX = 2.0f * (MouseX + 0.5f) / ViewportWidth  - 1.0f;
    real32 NdcY = 1.0f - 2.0f * (MouseY + 0.5f) / ViewportHeight;

    real32 TanHalf = tanf(FovYRadians * 0.5f);
    real32 Aspect  = ViewportWidth / ViewportHeight;

    ray Result;
    Result.Origin    = CameraPosition;
    Result.Direction = Normalize(CameraRight * (NdcX * TanHalf * Aspect) + CameraUp * (NdcY * TanHalf) + CameraForward);

    return Result;
}

#endif
