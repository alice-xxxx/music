import QtQuick
import QtQuick.Controls
import "../theme"

Menu {
    id: root
    property Item anchorItem
    property bool registeredOpen: false
    parent: Overlay.overlay
    popupType: Popup.Item
    modal: true
    dim: false
    focus: true
    margins: 8
    width: Math.min(224, Math.max(0, parent ? parent.width - 16 : 224))
    padding: 6
    background: Rectangle {
        color: Theme.surface
        radius: Theme.radius
        border.color: Theme.border
        border.width: 1
    }

    function reposition() {
        if (!anchorItem || !parent)
            return;
        const point = anchorItem.mapToItem(parent, 0, 0);
        const margin = 8;
        const rightAligned = point.x + anchorItem.width - width;
        const leftAligned = point.x;
        const preferredX = rightAligned >= margin ? rightAligned :
                           leftAligned + width <= parent.width - margin ? leftAligned : rightAligned;
        x = Math.max(margin, Math.min(parent.width - width - margin, preferredX));
        const below = point.y + anchorItem.height + 4;
        const above = point.y - height - 4;
        y = below + height <= parent.height - margin ? below :
            above >= margin ? above :
            Math.max(margin, Math.min(parent.height - height - margin, below));
    }

    function openAt(item) {
        anchorItem = item;
        reposition();
        open();
    }

    function registerOpen(opened) {
        if (registeredOpen === opened)
            return;
        registeredOpen = opened;
        Theme.openMenuCount += opened ? 1 : -1;
    }

    onOpened: registerOpen(true)
    onClosed: registerOpen(false)
    Component.onDestruction: registerOpen(false)
    onHeightChanged: if (opened) reposition()
    onWidthChanged: if (opened) reposition()
}
