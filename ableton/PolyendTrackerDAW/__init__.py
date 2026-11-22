from _Framework.Capabilities import (
    CONTROLLER_ID_KEY,
    NOTES_CC,
    PORTS_KEY,
    REMOTE,
    SCRIPT,
    controller_id,
    inport,
    outport,
)

from .PolyendTrackerDAW import PolyendTrackerDAW


def create_instance(c_instance):
    return PolyendTrackerDAW(c_instance)


def get_capabilities():
    # USB ids of the original Polyend Tracker (platforms/mk66 usb_desc.h).
    return {
        CONTROLLER_ID_KEY: controller_id(vendor_id=0x16D0, product_ids=[0x0E9F], model_name="Tracker"),
        PORTS_KEY: [
            inport(props=[NOTES_CC, SCRIPT, REMOTE]),
            outport(props=[SCRIPT, REMOTE]),
        ],
    }
