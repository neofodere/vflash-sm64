REGIONS = {
    "us": {"sha1": "9bef1128717f958171a4afac3ed78ee2bb4e86ce", "sound": (5748512, 97856, 5846368, 2216704)},
    "jp": {"sha1": "8a20a5c83d6ceb0f0506cfc9fa20d8f438cafe51", "sound": (5738816, 94400, 5833216, 1793408)},
    "eu": {"sha1": "4ac5721683d0e0b6bbb561b58a71740845dceea9", "sound": (5626032, 97856, 5723888, 2216704)},
}


def defines(region):
    return ["-DTARGET_N64", "-D_LANGUAGE_C", "-DVERSION_%s=1" % region.upper(), "-DF3D_OLD=1", "-DNON_MATCHING=1",
            "-DAVOID_UB=1"]
