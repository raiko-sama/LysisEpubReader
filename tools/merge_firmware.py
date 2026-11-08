# merge_firmware.py: produce a single flashable image with
# bootloader at 0x0, partitions at 0x8000 and application at 0x10000,
# padded to 8MB.
import os
import sys

Import("env")


def merge_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    core_dir = env.subst("$PROJECT_CORE_DIR")
    framework_dir = None
    try:
        framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    except Exception:
        pass
    out = os.path.join(project_dir, "lysis-reader-%s.bin" % env["PIOENV"])
    esptool = os.path.join(core_dir, "packages", "tool-esptoolpy", "esptool.py")
    firmware = os.path.join(build_dir, "firmware.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    if not os.path.isfile(firmware) or not os.path.isfile(partitions):
        print("merge_firmware: missing firmware/partitions")
        return

    bootloader = os.path.join(build_dir, "bootloader.bin")
    prebuilt = None
    # For QEMU runs the framework bootloader probes the flash chip with
    # READ_ID/SFDP commands the emulator does not implement. The stock
    # bootloader extracted from the original firmware image is used
    # instead so that the application itself can be exercised.
    if env["PIOENV"] == "qemu":
        stock = os.path.join(project_dir, "tools", "orig_bootloader.bin")
        if os.path.isfile(stock):
            bootloader = stock
            print("merge_firmware: using stock bootloader for QEMU")
    if framework_dir:
        candidate = os.path.join(framework_dir, "tools", "sdk", "esp32s3", "bin",
                                 "bootloader_qio_80m.elf")
        if os.path.isfile(candidate):
            prebuilt = candidate
    if prebuilt and env["PIOENV"] != "qemu":
        bl_out = os.path.join(build_dir, "bootloader_qio_dio.bin")
        cmd = [sys.executable, esptool, "--chip", "esp32s3", "elf2image",
               "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "8MB",
               "-o", bl_out, prebuilt]
        print("merge_firmware: building bootloader from", prebuilt)
        env.Execute(" ".join('"%s"' % c for c in cmd))
        if os.path.isfile(bl_out):
            bootloader = bl_out

    cmd = [
        sys.executable, esptool,
        "--chip", "esp32s3", "merge_bin",
        "--fill-flash-size", "8MB",
        "-o", out,
        "0x0", bootloader,
        "0x8000", partitions,
        "0x10000", firmware,
    ]
    print("merge_firmware:", " ".join(cmd))
    env.Execute(" ".join('"%s"' % c for c in cmd))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)
