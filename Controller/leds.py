def _set_cached(
    self,
    kind: str,
    module: int,
    value: int | bool,
) -> None:

    key = (
        kind,
        module,
    )

    if self.cache.get(key) == value:
        return

    self.cache[key] = value

    if kind == "BLUE":
        self.serial.set_blue(
            module,
            int(value),
        )

    elif kind == "RED":
        self.serial.set_red(
            module,
            bool(value),
        )

    elif kind == "GREEN":
        self.serial.set_green(
            module,
            bool(value),
        )