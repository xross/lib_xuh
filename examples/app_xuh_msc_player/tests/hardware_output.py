"""Capture Pyxrun checker input without treating diagnostic stderr as test output."""


class HardwareOutput:
    def __init__(self):
        self.lines = []

    def run(self, lines):
        self.lines = list(lines)
        return True

    @property
    def text(self):
        return "\n".join(self.lines)
