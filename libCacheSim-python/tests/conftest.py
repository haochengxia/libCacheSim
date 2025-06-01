from __future__ import annotations

import os

import pytest

from libcachesim import Reader, TraceType, open_trace


@pytest.fixture
def mock_reader():
    data_file = os.path.join(  # noqa: PTH118
        os.path.dirname(os.path.dirname(os.path.dirname(__file__))),  # noqa: PTH120
        "data",
        "cloudPhysicsIO.oracleGeneral.bin"
    )
    reader: Reader = open_trace(
        data_file,
        type=TraceType.ORACLE_GENERAL_TRACE.value,
    )
    return reader


