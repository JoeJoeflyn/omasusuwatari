import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui

BarWidget {
  id: root
  moduleName: "io.github.giogio.omasusuwatari"

  property int cpuPercent: 0
  property int frameIndex: 0
  property real jumpOffset: 0.0
  property bool isRunning: false
  property bool isHovered: false
  property real mouseRelX: 0.0
  property real mouseRelY: 0.0

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  function parseCpu(text) {
    var lines = String(text || "").split("\n")
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].trim()
      if (line.indexOf("cpu") === 0) {
        var parts = line.split(/\s+/)
        var pct = parseInt(parts[1], 10)
        return isFinite(pct) ? Math.max(0, Math.min(100, pct)) : 0
      }
    }
    return 0
  }

  function refresh() {
    if (!statsProc.running) statsProc.running = true
    if (!checkProc.running) checkProc.running = true
  }

  IpcHandler {
    target: "io.github.antigravity.omasusuwatari"
    function refresh(): void { root.broadcast("refresh") }
  }

  Process {
    id: ensureSetup
    command: ["sh", "-c", "if [ ! -x ~/.local/bin/susuwatari ] || [ ! -x ~/.local/bin/susuwatari-toggle ]; then ~/.config/omarchy/plugins/omasusuwatari/setup.sh; fi"]
    running: true
  }

  Process {
    id: statsProc
    command: ["omarchy-system-stats"]
    running: false
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: root.cpuPercent = root.parseCpu(text)
    }
  }

  Process {
    id: checkProc
    command: ["pgrep", "-x", "susuwatari"]
    running: false
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        root.isRunning = (text.trim().length > 0)
      }
    }
  }

  Timer {
    interval: 2000
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: root.refresh()
  }

  Timer {
    interval: root.isRunning ? Math.max(70, 140 - root.cpuPercent) : 300
    running: true
    repeat: true
    onTriggered: {
      root.frameIndex = (root.frameIndex + 1) % 16
    }
  }

  SequentialAnimation {
    id: clickBounce
    NumberAnimation { target: root; property: "jumpOffset"; to: -6; duration: 90; easing.type: Easing.OutQuad }
    NumberAnimation { target: root; property: "jumpOffset"; to: 0; duration: 140; easing.type: Easing.OutBounce }
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    slotSize: Style.bar.iconSlot
    tooltipText: root.isRunning ? ("Susuwatari: Active • CPU " + root.cpuPercent + "% (Click to Sleep)") : "Susuwatari: Sleeping (Click to Wake)"

    iconComponent: Component {
      Item {
        anchors.centerIn: parent
        width: Style.bar.iconCanvas
        height: Style.bar.iconCanvas

        Canvas {
          id: spriteCanvas
          anchors.fill: parent
          antialiasing: true

          Connections {
            target: root
            function onFrameIndexChanged() { spriteCanvas.requestPaint() }
            function onIsRunningChanged() { spriteCanvas.requestPaint() }
            function onIsHoveredChanged() { spriteCanvas.requestPaint() }
            function onMouseRelXChanged() { spriteCanvas.requestPaint() }
            function onMouseRelYChanged() { spriteCanvas.requestPaint() }
            function onJumpOffsetChanged() { spriteCanvas.requestPaint() }
          }

          onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var cx = width * 0.5
            var cy = height * 0.52 + root.jumpOffset
            var t = root.frameIndex * 0.4
            var r = Math.min(width, height) * 0.33

            var isPanicking = (root.cpuPercent > 60 && root.isRunning)

            // Draw clean fluff body
            var numSpikes = 20
            ctx.save()
            ctx.translate(cx, cy)

            // Outer soft contrast rim
            ctx.fillStyle = root.isRunning ? "rgba(255, 255, 255, 0.28)" : "rgba(180, 180, 200, 0.10)"
            ctx.beginPath()
            ctx.arc(0, 0, r + 2.0, 0, Math.PI * 2)
            ctx.fill()

            // Inky fluff body
            ctx.fillStyle = root.isRunning ? "#0c0c10" : "#1a1a22"
            ctx.beginPath()
            for (var i = 0; i < numSpikes; i++) {
              var angle = (i / numSpikes) * 2.0 * Math.PI
              var wiggle = Math.sin(t * 4.0 + i * 1.5) * (isPanicking ? 1.8 : (root.isRunning ? 1.0 : 0.3))
              var rad = r + 1.8 + wiggle
              var px = Math.cos(angle) * rad
              var py = Math.sin(angle) * rad
              if (i === 0) ctx.moveTo(px, py)
              else ctx.lineTo(px, py)
            }
            ctx.closePath()
            ctx.fill()

            // Inner body core
            ctx.fillStyle = root.isRunning ? "#161620" : "#22222a"
            ctx.beginPath()
            ctx.arc(0, 0, r * 0.88, 0, Math.PI * 2)
            ctx.fill()

            // Stick feet
            if (root.isRunning) {
              ctx.strokeStyle = "#0c0c10"
              ctx.lineWidth = 1.3
              ctx.lineCap = "round"
              var step = Math.sin(t * 5.0) * 1.5
              ctx.beginPath()
              ctx.moveTo(-r * 0.5, r * 0.6)
              ctx.lineTo(-r * 0.7, r * 0.95 + step)
              ctx.moveTo(r * 0.5, r * 0.6)
              ctx.lineTo(r * 0.7, r * 0.95 - step)
              ctx.stroke()
            }

            // Reaching hands when hovered
            if (root.isHovered && root.isRunning) {
              ctx.beginPath()
              ctx.moveTo(-r * 0.4, -r * 0.2)
              ctx.lineTo(-r * 0.8, -r * 0.9)
              ctx.moveTo(r * 0.4, -r * 0.2)
              ctx.lineTo(r * 0.8, -r * 0.9)
              ctx.stroke()
            }

            // Eyes
            var eyeDist = r * 0.38
            var eyeY = -r * 0.15
            var eyeR = r * 0.34
            var isBlinking = (root.frameIndex === 15)

            if (!root.isRunning) {
              // Sleeping eyes (˘ ˘)
              ctx.strokeStyle = "rgba(255, 255, 255, 0.8)"
              ctx.lineWidth = 1.5
              ctx.beginPath()
              ctx.arc(-eyeDist, eyeY, eyeR * 0.8, 0.4, Math.PI - 0.4)
              ctx.stroke()
              ctx.beginPath()
              ctx.arc(eyeDist, eyeY, eyeR * 0.8, 0.4, Math.PI - 0.4)
              ctx.stroke()

              // Floating Zzz
              ctx.fillStyle = "rgba(200, 220, 255, 0.8)"
              ctx.font = "bold 9px sans-serif"
              ctx.fillText("z", r * 0.6, -r * 0.6 + Math.sin(t * 2.0) * 2.0)
            } else {
              // Awake eyes
              ctx.fillStyle = "#ffffff"
              ctx.beginPath()
              if (isBlinking) {
                ctx.ellipse(-eyeDist, eyeY, eyeR, eyeR * 0.15, 0, 0, Math.PI * 2)
                ctx.ellipse(eyeDist, eyeY, eyeR, eyeR * 0.15, 0, 0, Math.PI * 2)
              } else {
                ctx.arc(-eyeDist, eyeY, eyeR, 0, Math.PI * 2)
                ctx.arc(eyeDist, eyeY, eyeR, 0, Math.PI * 2)
              }
              ctx.fill()

              // Pupils
              if (!isBlinking) {
                var lookX = root.isHovered ? (root.mouseRelX * 1.4) : (Math.sin(t * 1.5) * 1.0)
                var lookY = root.isHovered ? (root.mouseRelY * 1.0) : 0

                ctx.fillStyle = "#0a0a0f"
                ctx.beginPath()
                ctx.arc(-eyeDist + lookX, eyeY + lookY, eyeR * 0.48, 0, Math.PI * 2)
                ctx.arc(eyeDist + lookX, eyeY + lookY, eyeR * 0.48, 0, Math.PI * 2)
                ctx.fill()

                ctx.fillStyle = "#ffffff"
                ctx.beginPath()
                ctx.arc(-eyeDist + lookX - 0.5, eyeY + lookY - 0.5, eyeR * 0.2, 0, Math.PI * 2)
                ctx.arc(eyeDist + lookX - 0.5, eyeY + lookY - 0.5, eyeR * 0.2, 0, Math.PI * 2)
                ctx.fill()
              }
            }

            // Sweat drop when high CPU load
            if (isPanicking) {
              ctx.fillStyle = "#66ccff"
              ctx.beginPath()
              ctx.arc(r * 0.8, -r * 0.7 + Math.sin(t * 6.0) * 1.2, 1.6, 0, Math.PI * 2)
              ctx.fill()
            }

            ctx.restore()
          }
        }

        MouseArea {
          anchors.fill: parent
          hoverEnabled: true
          onEntered: { root.isHovered = true }
          onExited: { root.isHovered = false; root.mouseRelX = 0; root.mouseRelY = 0 }
          onPositionChanged: function(mouse) {
            root.mouseRelX = Math.max(-1.0, Math.min(1.0, (mouse.x - width * 0.5) / (width * 0.5)))
            root.mouseRelY = Math.max(-1.0, Math.min(1.0, (mouse.y - height * 0.5) / (height * 0.5)))
          }
        }
      }
    }

    onPressed: function() {
      clickBounce.start()
      if (root.bar) {
        root.bar.run("susuwatari-toggle")
      }
      checkTimer.restart()
    }
  }

  Timer {
    id: checkTimer
    interval: 250
    repeat: false
    onTriggered: root.refresh()
  }
}
