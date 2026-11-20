#!/usr/bin/python3
#
# Copyright (C) 2011 Julian Andres Klode <jak@debian.org>
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.
"""Unit tests for verifying the correctness of apt_pkg.Configuration"""
import os
import subprocess
import sys
import sysconfig
import textwrap
import unittest

import apt_pkg
import testcommon


class TestConfiguration(testcommon.TestCase):
    """Test various configuration things"""

    def test_lp707416(self):
        """configuration: Test empty arguments (LP: #707416)"""
        self.assertRaises(ValueError, apt_pkg.parse_commandline, apt_pkg.config, [], [])
        self.assertRaises(
            SystemError,
            apt_pkg.parse_commandline,
            apt_pkg.config,
            [],
            ["cmd", "--arg0"],
        )

    @unittest.skipUnless(
        sysconfig.get_config_var("Py_GIL_DISABLED"), "requires free threading"
    )
    def test_native_modules_under_free_threading(self):
        """The bindings share libapt safely and preserve the default GIL state."""
        script = r"""
            from concurrent.futures import ThreadPoolExecutor
            import sys
            from threading import Barrier

            assert sys.flags.gil is None and not sys._is_gil_enabled()
            import apt_pkg
            import apt_inst

            assert not sys._is_gil_enabled()
            apt_pkg.init()
            config = apt_pkg.Configuration()
            start = Barrier(8)

            def exercise(worker):
                key = f"test::worker::{worker}"
                options = [("f", "flag", key, "HasArg")]
                start.wait()
                for index in range(64):
                    value = str(index)
                    assert apt_pkg.parse_commandline(config, options, ["probe", "--flag", value]) == []
                    assert config.find(key) == value
                    assert apt_pkg.TagSection("Package: example\n")["Package"] == "example"
                    with open("data/test_debs/utf8-package_1.0-1_all.deb", "rb") as package:
                        assert "debian-binary" in apt_inst.ArArchive(package).getnames()
                with open("data/test_debs/utf8-package_1.0-1_all.deb", "rb") as package:
                    assert apt_inst.DebFile(package).control.extractdata("control")

            with ThreadPoolExecutor(max_workers=8) as pool:
                list(pool.map(exercise, range(8)))
            assert not sys._is_gil_enabled()
            """
        env = os.environ.copy()
        env.pop("PYTHON_GIL", None)
        subprocess.run(
            [sys.executable, "-c", textwrap.dedent(script)],
            env=env,
            check=True,
            timeout=60,
        )


if __name__ == "__main__":
    unittest.main()
