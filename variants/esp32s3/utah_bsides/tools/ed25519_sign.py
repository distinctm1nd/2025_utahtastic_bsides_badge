"""Ed25519 signing (RFC 8032), pure Python so builds need no extra packages. Slow, but the
badge build signs one 32-byte digest per firmware."""
import hashlib

_p = 2**255 - 19
_q = 2**252 + 27742317777372353535851937790883648493
_d = -121665 * pow(121666, _p - 2, _p) % _p
_I = pow(2, (_p - 1) // 4, _p)


def _inv(x):
    return pow(x, _p - 2, _p)


def _xrecover(y):
    xx = (y * y - 1) * _inv(_d * y * y + 1)
    x = pow(xx, (_p + 3) // 8, _p)
    if (x * x - xx) % _p:
        x = x * _I % _p
    return _p - x if x % 2 else x


_By = 4 * _inv(5) % _p
_Bx = _xrecover(_By)
_B = (_Bx, _By, 1, _Bx * _By % _p)


def _add(P, Q):
    a = (P[1] - P[0]) * (Q[1] - Q[0]) % _p
    b = (P[1] + P[0]) * (Q[1] + Q[0]) % _p
    c = 2 * P[3] * Q[3] * _d % _p
    dd = 2 * P[2] * Q[2] % _p
    e, f, g, h = b - a, dd - c, dd + c, b + a
    return (e * f % _p, g * h % _p, f * g % _p, e * h % _p)


def _mul(s, P):
    Q = (0, 1, 1, 0)
    while s:
        if s & 1:
            Q = _add(Q, P)
        P = _add(P, P)
        s >>= 1
    return Q


def _encode(P):
    zi = _inv(P[2])
    x, y = P[0] * zi % _p, P[1] * zi % _p
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


def _expand(seed):
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed: bytes) -> bytes:
    return _encode(_mul(_expand(seed)[0], _B))


def sign(seed: bytes, msg: bytes) -> bytes:
    a, prefix = _expand(seed)
    A = _encode(_mul(a, _B))
    r = int.from_bytes(hashlib.sha512(prefix + msg).digest(), "little") % _q
    R = _encode(_mul(r, _B))
    k = int.from_bytes(hashlib.sha512(R + A + msg).digest(), "little") % _q
    return R + ((r + k * a) % _q).to_bytes(32, "little")
