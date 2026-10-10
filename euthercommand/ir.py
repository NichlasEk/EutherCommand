"""Bounded mark/space timings in microseconds, starting with a mark."""

def validate(signal):
    if not isinstance(signal, dict) or set(signal) != {'carrier_hz', 'durations_us'}:
        raise ValueError('signal requires carrier_hz and durations_us')
    carrier, pulses = signal['carrier_hz'], signal['durations_us']
    if type(carrier) is not int or not 30000 <= carrier <= 60000:
        raise ValueError('carrier out of range')
    if not isinstance(pulses, list) or not 3 <= len(pulses) <= 511 or len(pulses) % 2 != 1:
        raise ValueError('expected 3..511 alternating durations ending in mark')
    if any(type(x) is not int or not 100 <= x <= 30000 for x in pulses) or sum(pulses) > 250000:
        raise ValueError('invalid or excessive duration')
    return signal


def nec(address, command):
    if any(type(x) is not int or not 0 <= x <= 255 for x in (address, command)):
        raise ValueError('NEC byte out of range')
    pulses = [9000, 4500]
    for byte in (address, address ^ 255, command, command ^ 255):
        for bit in range(8):
            pulses.extend((560, 1690 if byte & (1 << bit) else 560))
    return validate({'carrier_hz': 38000, 'durations_us': pulses + [560]})


def sirc(address, command, bits=12):
    if bits != 12 or not 0 <= address < 32 or not 0 <= command < 128:
        raise ValueError('only 12-bit SIRC encoding supported')
    value = command | address << 7
    pulses = [2400, 600]
    for i in range(bits):
        pulses.extend((1200 if value & (1 << i) else 600, 600))
    return validate({'carrier_hz': 40000, 'durations_us': pulses[:-1]})


def decode(signal):
    p = validate(signal)['durations_us']
    near = lambda actual, expected: abs(actual - expected) <= expected * .25
    if len(p) == 67 and near(p[0], 9000) and near(p[1], 4500) and all(near(x, 560) for x in p[2::2]):
        spaces = p[3::2]
        if all(near(x, 560) or near(x, 1690) for x in spaces):
            value = sum((x > 1000) << i for i, x in enumerate(spaces))
            a, ai, c, ci = [(value >> shift) & 255 for shift in (0, 8, 16, 24)]
            if a ^ ai == 255 and c ^ ci == 255:
                return {'protocol': 'NEC', 'address': a, 'command': c}
    if len(p) in (25, 31, 41) and near(p[0], 2400) and all(near(x, 600) for x in p[1::2]):
        marks = p[2::2]
        if all(near(x, 600) or near(x, 1200) for x in marks):
            return {'protocol': 'SIRC', 'bits': len(marks), 'value': sum((x > 900) << i for i, x in enumerate(marks))}
    return {'protocol': 'raw'}
