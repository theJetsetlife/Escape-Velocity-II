#!/usr/bin/env python3
"""Synthetic reproduction of the historical SAMU HMAC length-validation flaw.

This is a host-side policy model only: it uses a fixed public test fixture,
not any PS4 interface, secret, process memory, or crypto keyslot.
"""

import hashlib
import hmac
import json
from dataclasses import dataclass

SYNTHETIC_KEY = bytes(range(1, 33))  # Public deterministic test vector.
SYNTHETIC_MESSAGE = b"Escape Velocity II: synthetic HMAC policy check"
MINIMUM_HARDENED_LENGTH = 16
TEST_LENGTHS = (1, 15, 16, 32)


class KeyLengthRejected(ValueError):
    """Requested HMAC key length is rejected by the policy."""


@dataclass(frozen=True)
class HmacPolicy:
    name: str
    minimum_length: int

    def digest(self, requested_length: int) -> bytes:
        """Compute a test-vector MAC using only a fixed synthetic key prefix."""
        if not isinstance(requested_length, int) or isinstance(requested_length, bool):
            raise TypeError("length must be an integer")
        if requested_length < self.minimum_length:
            raise KeyLengthRejected("key length below policy minimum")
        if not 1 <= requested_length <= len(SYNTHETIC_KEY):
            raise KeyLengthRejected("key length outside fixture bounds")
        return hmac.new(
            SYNTHETIC_KEY[:requested_length], SYNTHETIC_MESSAGE, hashlib.sha256
        ).digest()


def build_report() -> dict:
    policies = (
        HmacPolicy("historical_unchecked_model", 1),
        HmacPolicy("hardened_length_check_model", MINIMUM_HARDENED_LENGTH),
    )
    rows = []
    for length in TEST_LENGTHS:
        row = {"requested_length": length}
        for policy in policies:
            try:
                policy.digest(length)
                row[policy.name] = "accepted"
            except KeyLengthRejected:
                row[policy.name] = "rejected"
        rows.append(row)
    return {
        "schema": "evii.synthetic-samu-hmac-policy.v1",
        "target": "offline_public_fixture_only",
        "firmware_900_support": "not_applicable_model_only",
        "minimum_hardened_key_length": MINIMUM_HARDENED_LENGTH,
        "results": rows,
    }


if __name__ == "__main__":
    print(json.dumps(build_report(), indent=2))
