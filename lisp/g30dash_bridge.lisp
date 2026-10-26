; G30 VESC dashboard - VESC side script
;
; Single wire mode (bridge 1):
;   The dash talks to the VESC over the stock yellow wire, which is
;   connected to the COMM TX pin in half duplex mode. This script hands
;   every frame to the VESC command interface (cmds-proc), so throttle,
;   telemetry, riding modes and VESC Tool over the dash BLE all work.
;   The green wire (dash button) goes to the COMM RX pin with a 1k pull-up
;   to 3.3V.
;
; Two wire mode (bridge 0):
;   The VESC UART app talks to the dash directly. This script only reports
;   the button if it is wired to a VESC pin (set btn-pin).
;
; Needs VESC firmware 6.06 or newer.
; Note: stopping or re-uploading this script over the dash BLE cuts the
; link in single wire mode. Edit it over USB.

; ---------------------------------------------------------------- settings

(def bridge 1)

; Pin of the dash button (pressed = low), nil if not wired to the VESC.
; In two wire mode the RX pin is taken by the UART, use e.g. 'pin-ppm.
(def btn-pin 'pin-rx)

; ---------------------------------------------------------------- button

(def btn-level 0)   ; 1 while pressed
(def btn-count 0)   ; increments on every press, wraps at 256

(defun btn-read () (if (= (gpio-read btn-pin) 0) 1 0))

(defun btn-thread ()
    (loopwhile t {
            (var a (btn-read))
            (sleep 0.01)
            (var b (btn-read))
            (if (= a b) {
                    (if (and (= a 1) (= btn-level 0))
                        (setq btn-count (mod (+ btn-count 1) 256))
                    )
                    (setq btn-level a)
            })
            (sleep 0.01)
}))

; ---------------------------------------------------------------- events

; Dash button poll: ['G' 'D' 0x01] -> ['G' 'D' 0x81 level count]
(defun proc-app-data (data)
    (if (and (>= (buflen data) 3)
            (= (bufget-u8 data 0) 0x47)
            (= (bufget-u8 data 1) 0x44)
            (= (bufget-u8 data 2) 0x01))
        (send-data (list 0x47 0x44 0x81 btn-level btn-count))
))

(defun event-handler () {
        (set-mailbox-size 10)
        (loopwhile t
            (recv
                ((event-cmds-data-tx (? data)) (uart-write data))
                ((event-data-rx . (? data)) (proc-app-data data))
                (_ nil)
        ))
})

; ---------------------------------------------------------------- bridge

(def frame (bufcreate 600))

; Read n bytes into buf at ofs, uart-read takes at most 512 per call.
(defun read-n (buf n ofs) {
        (var got 0)
        (var ok t)
        (loopwhile (and ok (< got n)) {
                (var want (- n got))
                (if (> want 256) (setq want 256))
                (var r (uart-read buf want (+ ofs got) nil 0.02))
                (if (= r 0)
                    (setq ok nil)
                    (setq got (+ got r))
                )
        })
        got
})

; One VESC frame: [2 len | 3 lenH lenL] payload crcH crcL 3
; Frames are passed on one at a time with a short pause, the command
; interface drops a frame that arrives while the previous one still runs.
(defun read-frame () {
        (uart-read-bytes frame 1 0)
        (var hdr (bufget-u8 frame 0))
        (if (or (= hdr 2) (= hdr 3))
            (if (= (read-n frame (- hdr 1) 1) (- hdr 1)) {
                    (var len (if (= hdr 2) (bufget-u8 frame 1) (bufget-u16 frame 1)))
                    (var frame-rest (+ len 3))
                    (if (and (> len 0) (<= (+ hdr frame-rest) 600))
                        (if (= (read-n frame frame-rest hdr) frame-rest) {
                                (var pkt (bufcreate (+ hdr frame-rest)))
                                (bufcpy pkt 0 frame 0 (+ hdr frame-rest))
                                (cmds-proc pkt)
                                (free pkt)
                                (sleep 0.002)
                        })
                    )
            })
        )
})

; ---------------------------------------------------------------- start

(event-register-handler (spawn 256 event-handler))
(event-enable 'event-data-rx)

(if (= bridge 1) {
        (uart-start 115200 'half-duplex)
        (event-enable 'event-cmds-data-tx)
        (cmds-start-stop true)
})

; After uart-start, the RX pin is free in half duplex mode
(if btn-pin {
        (gpio-configure btn-pin 'pin-mode-in-pu)
        (spawn 100 btn-thread)
})

(if (= bridge 1)
    (loopwhile t (read-frame))
)
