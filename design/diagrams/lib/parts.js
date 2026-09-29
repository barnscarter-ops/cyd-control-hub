/* Server-fan part models (rendered concepts, not CAD). Origin conventions are documented per builder. */
(function () {
  const T = THREE, K = Kit;
  const P = {};

  // ---------- HW678 ESP32-S3 N16R8 (photo-confirmed layout, inventory id s3)
  const LEFT = ['3V3', '3V3', 'RST', '4', '5', '6', '7', '15', '16', '17', '18', '8', '3', '46', '9', '10', '11', '12', '13', '14', '5V', 'GND'];
  const RIGHT = ['GND', 'TX', 'RX', '1', '2', '42', '41', '40', '39', '38', '37', '36', '35', '0', '45', '48', '47', '21', '20', '19', 'GND', 'GND'];
  P.S3_LEFT = LEFT; P.S3_RIGHT = RIGHT;
  // Group origin: pin-tip plane, board centre. Module (antenna) end is +x; pins at z=-11.43 (LEFT list, far) and +11.43 (RIGHT list, near).
  // Pin k sits at x = 26.67 - k*2.54.
  P.s3PinX = (k) => 26.67 - k * 2.54;
  P.esp32s3 = function (o) {
    o = o || {};
    const hdr = o.header == null ? 2.5 : o.header;
    const g = new T.Group();
    const used = { '4': 1, '5': 1, '6': 1, '7': 1, '8': 1, '3V3': 1, 'GND': 1, '5V': 1 };
    const W = 63.4, D = 25.4, PX = 20;
    const top = K.tex(Math.round(W * PX), Math.round(D * PX), (c, w, h) => {
      c.fillStyle = '#0d1114'; c.fillRect(0, 0, w, h);
      c.fillStyle = '#1a2126'; c.fillRect(4, 4, w - 8, h - 8);
      const X = (x) => (x + W / 2) * PX, Z = (z) => (z + D / 2) * PX;
      c.font = '600 27px system-ui,sans-serif'; c.textAlign = 'center'; c.textBaseline = 'middle';
      const drawList = (list, z, inward) => list.forEach((n, k) => {
        c.fillStyle = used[n] ? '#ffd166' : '#dfe6e9';
        c.fillText(n, X(P.s3PinX(k)), Z(z + inward * 3.6));
      });
      drawList(LEFT, -11.43, 1); drawList(RIGHT, 11.43, -1);
      c.fillStyle = '#c9d1d3'; c.font = '600 25px system-ui,sans-serif';
      c.fillText('RST', X(-10), Z(-5)); c.fillText('BOOT', X(-10), Z(3.2));
      c.fillStyle = '#8fa0a5'; c.font = '600 21px system-ui,sans-serif';
      c.fillText('HW678 · ESP32-S3 N16R8', X(-4), Z(9.2));
    });
    const pcbMats = [Kit.mat(0x0d1114), Kit.mat(0x0d1114), new T.MeshStandardMaterial({ map: top, roughness: 0.6 }), Kit.mat(0x0d1114), Kit.mat(0x0d1114), Kit.mat(0x0d1114)];
    const yb = hdr + 0.8;
    const pcb = K.box(g, [W, 1.6, D], [0, yb, 0], pcbMats[0]);
    pcb.material = pcbMats;
    // headers + pins
    const metal = Kit.mat(0xd8c27a, { metalness: 0.7, roughness: 0.3 });
    const plastic = Kit.mat(0x14181b);
    [[-11.43, LEFT], [11.43, RIGHT]].forEach(([z]) => {
      K.box(g, [22 * 2.54, hdr, 2.54], [0, hdr / 2, z], plastic);
      for (let k = 0; k < 22; k++) {
        const x = P.s3PinX(k);
        K.box(g, [0.64, hdr + 3.6, 0.64], [x, (hdr - 3.6) / 2 + hdr / 2 - hdr / 2 + 0.0, z], metal).position.y = (hdr - 3.6) / 2 + 0.6;
        K.box(g, [0.9, 0.5, 0.9], [x, yb + 0.9, z], metal);
      }
    });
    // Wi-Fi module + antenna keep-out
    const ytop = yb + 0.8;
    const mod = new T.Group(); g.add(mod);
    K.box(mod, [25.5, 3.0, 18], [W / 2 - 25.5 / 2 - 0.2, ytop + 1.5, 0], Kit.mat(0xc9ced1, { metalness: 0.75, roughness: 0.35 }));
    K.box(mod, [6.5, 0.8, 18], [W / 2 - 25.5 - 0.2 - 3.25 + 25.5 + 0.0 - 0.0, ytop + 0.4, 0], Kit.mat(0x111417)).position.x = W / 2 - 3.25 - 0.2;
    mod.children[0].position.x = W / 2 - 6.5 - 25.5 / 2 + 0.4;
    const modTex = K.tex(512, 360, (c, w, h) => {
      c.fillStyle = '#c9ced1'; c.fillRect(0, 0, w, h);
      c.fillStyle = '#5b6468'; c.font = '600 44px system-ui,sans-serif'; c.textAlign = 'center';
      c.fillText('ESP32-S3-N16R8', w / 2, 140); c.font = '600 32px system-ui,sans-serif';
      c.fillText('WiFi+BT MODEL', w / 2, 200); c.fillText('FC CE SRRC', w / 2, 250); c.font = '26px system-ui,sans-serif';
      c.fillText('ISM 2.4G 802.11 b/g/n', w / 2, 300);
    });
    const dec = new T.Mesh(new T.PlaneGeometry(25.3, 17.8), new T.MeshBasicMaterial({ map: modTex }));
    dec.rotation.x = -Math.PI / 2; dec.rotation.z = Math.PI / 2; dec.position.set(mod.children[0].position.x, ytop + 3.02, 0); mod.add(dec);
    // small parts seen in the photo: regulator, buttons, RGB LED, USB-C x2
    K.box(g, [6.5, 1.6, 3.5], [-2, ytop + 0.8, -5.6], Kit.mat(0x22282c));
    K.box(g, [6.5, 0.4, 1.6], [-2, ytop + 0.2, -8.0], Kit.mat(0xb7bcc0, { metalness: 0.6 }));
    K.cyl(g, 1.3, 1.6, [-10, ytop + 1.6, -2.2], 0x2b3236, { axis: 'y' });
    K.cyl(g, 1.3, 1.6, [-10, ytop + 1.6, 5.6], 0x2b3236, { axis: 'y' });
    K.box(g, [4.6, 1.2, 4.6], [-5, ytop + 0.6, 7.4], Kit.mat(0xe9ecee, { emissive: 0x0a0a20 }));
    K.box(g, [8.9, 3.3, 7.4], [-W / 2 + 3.6, ytop + 1.65, -4.2], Kit.mat(0xcfd4d7, { metalness: 0.85, roughness: 0.3 }));
    K.box(g, [8.9, 3.3, 7.4], [-W / 2 + 3.6, ytop + 1.65, 4.2], Kit.mat(0xcfd4d7, { metalness: 0.85, roughness: 0.3 }));
    K.box(g, [3.4, 1.4, 6.2], [-W / 2 + 0.6, ytop + 1.65, -4.2], Kit.mat(0x06090a));
    K.box(g, [3.4, 1.4, 6.2], [-W / 2 + 0.6, ytop + 1.65, 4.2], Kit.mat(0x06090a));
    return g;
  };

  // ---------- 120 mm four-wire fan. Origin: centre of the frame's lower face; frame spans y 0..25.
  P.fan = function (o) {
    o = o || {};
    const g = new T.Group();
    const R = 60, s = new T.Shape();
    const r = 6;
    s.moveTo(-R + r, -R); s.lineTo(R - r, -R); s.absarc(R - r, -R + r, r, -Math.PI / 2, 0); s.lineTo(R, R - r); s.absarc(R - r, R - r, r, 0, Math.PI / 2);
    s.lineTo(-R + r, R); s.absarc(-R + r, R - r, r, Math.PI / 2, Math.PI); s.lineTo(-R, -R + r); s.absarc(-R + r, -R + r, r, Math.PI, Math.PI * 1.5);
    const hole = new T.Path(); hole.absarc(0, 0, 56.5, 0, Math.PI * 2, true); s.holes.push(hole);
    [[-52.5, -52.5], [52.5, -52.5], [52.5, 52.5], [-52.5, 52.5]].forEach((p) => { const h = new T.Path(); h.absarc(p[0], p[1], 2.3, 0, Math.PI * 2, true); s.holes.push(h); });
    const geo = new T.ExtrudeGeometry(s, { depth: 25, bevelEnabled: false, curveSegments: 40 });
    geo.rotateX(-Math.PI / 2); geo.translate(0, 0, 0);
    // after rotateX(-90deg): shape y -> -z, extrusion +z -> +y
    const frame = new T.Mesh(geo, Kit.mat(o.frame || 0x1c2125, { roughness: 0.5 }));
    frame.castShadow = frame.receiveShadow = true; g.add(frame);
    K.cyl(g, 21, 24, [0, 12.5, 0], 0x0f1316);
    K.cyl(g, 17, 0.6, [0, 24.9, 0], 0x2a3238);
    const bm = Kit.mat(o.blade || 0xdfe7ea, { roughness: 0.4 });
    for (let i = 0; i < 9; i++) {
      const a = (i / 9) * Math.PI * 2, arm = new T.Group();
      const bl = new T.Mesh(new T.BoxGeometry(37, 1.3, 17), bm);
      bl.position.set(19 + 20, 0, 0); bl.rotation.x = 0.42; bl.castShadow = true;
      bl.position.x = 39; arm.add(bl);
      arm.rotation.y = a; arm.position.y = 12.5; g.add(arm);
    }
    // rear support struts
    [0, 1, 2, 3].forEach((i) => { const a = (i * Math.PI) / 2 + 0.6; K.box(g, [36, 2, 2.4], [Math.cos(a) * 38, 2, Math.sin(a) * 38], 0x151a1d).rotation.y = -a; });
    return g;
  };

  // ---------- through-hole & module builders
  P.to92 = function () {
    const g = new T.Group();
    const geo = new T.CylinderGeometry(2.4, 2.4, 5, 24, 1, false, -Math.PI * 0.62, Math.PI * 1.24);
    const m = new T.Mesh(geo, Kit.mat(0x14181b, { roughness: 0.5 }));
    m.position.y = 6.5; m.castShadow = true; g.add(m);
    K.box(g, [4.2, 5, 1.7], [0, 6.5, -0.15], 0x14181b).position.z = 0.42;
    [-2.54, 0, 2.54].forEach((x) => K.box(g, [0.5, 5.6, 0.5], [x, 2.1, 0], 0xc9cdd0, { metalness: 0.8 }));
    return g;
  };
  // Axial resistor lying along x; A/B are world (x,z) hole positions, y0 = breadboard top.
  P.resistor = function (parent, A, B, y0, bands, bodyLen) {
    const g = new T.Group(); parent.add(g);
    const cx = (A[0] + B[0]) / 2, cz = (A[1] + B[1]) / 2, yb = y0 + 3.2;
    const L = bodyLen || 6.3;
    K.cyl(g, 1.3, L, [cx, yb, cz], 0xd8b980, { axis: 'x' });
    K.cyl(g, 1.6, 1.1, [cx - L / 2 + 0.6, yb, cz], 0xd8b980, { axis: 'x' });
    K.cyl(g, 1.6, 1.1, [cx + L / 2 - 0.6, yb, cz], 0xd8b980, { axis: 'x' });
    const col = { br: 0x6b3b1e, bk: 0x0b0b0b, rd: 0xc22a2a, or: 0xe9812a, ye: 0xe6c92a, vi: 0x7a3fb0, gd: 0xc8a951 };
    const xs = [-2.0, -1.0, 0.0, 2.1];
    bands.forEach((b, i) => K.cyl(g, 1.36, 0.55, [cx + xs[i], yb, cz], col[b], { axis: 'x' }));
    const lead = (P0, dir) => K.tube(g, [[P0[0], y0 + 0.1, P0[1]], [P0[0], yb - 0.4, P0[1]], [cx + dir * L / 2, yb, cz]], 0xc9cdd0, 0.35, { tension: 0 });
    lead(A, -1); lead(B, 1);
    return g;
  };
  P.electrolytic = function (r, h) {
    const g = new T.Group();
    K.cyl(g, r, h, [0, h / 2, 0], 0x172a55);
    K.cyl(g, r * 0.96, 0.4, [0, h + 0.1, 0], 0xb9c0c4, { m: { metalness: 0.8 } });
    K.box(g, [r * 0.16, h * 0.86, r * 0.5], [-r * 0.98, h / 2, 0], 0xdfe6ea);
    return g;
  };
  P.axialDiode = function (len, bandCol) {
    const g = new T.Group();
    K.cyl(g, 2.6, len, [0, 0, 0], 0x15191c, { axis: 'x' });
    K.cyl(g, 2.7, 1.2, [len / 2 - 1.4, 0, 0], bandCol || 0xd9dde0, { axis: 'x' });
    K.cyl(g, 0.5, len + 14, [0, 0, 0], 0xc9cdd0, { axis: 'x', m: { metalness: 0.8 } });
    return g;
  };
  P.terminal = function (n, pitch, col) {
    const g = new T.Group(); const w = n * pitch;
    K.box(g, [w, 9.5, 8.5], [0, 4.75, 0], col || 0x2a5f9e);
    for (let i = 0; i < n; i++) {
      const x = (i - (n - 1) / 2) * pitch;
      K.cyl(g, 1.9, 0.9, [x, 9.9, 0], 0xb9bfc3, { m: { metalness: 0.8 } });
      K.box(g, [3.2, 3.6, 0.6], [x, 4.2, 4.4], 0x0a0d0f);
    }
    return g;
  };
  P.conn4 = function () { // white keyed 4-pin fan plug
    const g = new T.Group();
    K.box(g, [11, 6, 6], [0, 3, 0], 0xecebe4);
    K.box(g, [5, 1.4, 4], [0, 6.4, 0], 0xecebe4);
    K.box(g, [1.6, 3, 5], [0, 3, 3], 0xd8d7cf);
    return g;
  };
  P.psu = function () {
    const g = new T.Group();
    const tex = K.tex(660, 300, (c, w, h) => {
      c.fillStyle = '#202629'; c.fillRect(0, 0, w, h);
      c.fillStyle = '#e8f3f5'; c.font = '700 52px system-ui,sans-serif'; c.fillText('12 V  DC', 32, 84);
      c.font = '600 34px system-ui,sans-serif'; c.fillStyle = '#9db0b6'; c.fillText('regulated · ≥2 A listed', 32, 136);
      c.fillStyle = '#ffb45b'; c.font = '600 28px system-ui,sans-serif'; c.fillText('exact unit unresolved (PSU-01)', 32, 190);
      c.fillStyle = '#4bd66a'; c.beginPath(); c.arc(590, 60, 14, 0, 7); c.fill();
    });
    const mats = [0, 0, 0, 0, 0, 0].map(() => Kit.mat(0x22292c));
    mats[2] = new T.MeshStandardMaterial({ map: tex, roughness: 0.6 });
    const b = K.box(g, [110, 30, 50], [0, 15, 0], mats[0]); b.material = mats;
    K.box(g, [110, 3, 4], [0, 1.5, 24], 0x151a1c);
    return g;
  };
  P.fuseHolder = function () {
    const g = new T.Group();
    K.box(g, [11, 11, 11], [-15, 5.5, 0], 0x14181a); K.box(g, [11, 11, 11], [15, 5.5, 0], 0x14181a);
    const f = K.cyl(g, 2.9, 22, [0, 5.5, 0], 0xe9d9a8, { axis: 'x', m: { transparent: true, opacity: 0.55, roughness: 0.15 } });
    f.material.userData.ghost = false;
    K.cyl(g, 3.05, 4, [-9, 5.5, 0], 0xc8ccd0, { axis: 'x', m: { metalness: 0.85 } });
    K.cyl(g, 3.05, 4, [9, 5.5, 0], 0xc8ccd0, { axis: 'x', m: { metalness: 0.85 } });
    K.cyl(g, 0.22, 16, [0, 5.5, 0], 0x6a5a3a, { axis: 'x' });
    return g;
  };
  P.buck = function () {
    const g = new T.Group();
    K.box(g, [43, 1.6, 21], [0, 3.6, 0], 0x1f5a8c);
    K.box(g, [12, 8, 12], [-6, 8.4, -1], 0x1b1f22);           // inductor
    K.cyl(g, 4.2, 12, [12, 10.2, 5], 0x172a55);
    K.cyl(g, 4.2, 12, [12, 10.2, -5.5], 0x172a55);
    K.box(g, [10, 4, 8], [-17, 6.6, 5], 0x14181a);            // controller IC
    K.box(g, [5, 4, 5], [-17, 6.6, -6], 0x2b7fd8);            // trim pot
    K.box(g, [8, 6, 4.5], [-19, 7.4, 0], 0x2a5f9e).position.set(-19, 7.4, 0);
    K.box(g, [4, 2.2, 21], [20.5, 4.6, 0], 0x22a04a).scale.set(1, 1, 0.98);
    return g;
  };
  P.probe = function () {
    const g = new T.Group();
    K.cyl(g, 3, 50, [25, 0, 0], 0xc2c9cc, { axis: 'x', m: { metalness: 0.85, roughness: 0.28 } });
    K.cyl(g, 3.6, 8, [-1, 0, 0], 0x14181a, { axis: 'x' });
    return g;
  };

  // 3.5" CYD (illustrative ~98 x 58 mm), landscape, screen up, USB-C on the left edge. Origin: board centre, y = board bottom.
  P.cyd = function () {
    const g = new T.Group();
    const scr = K.tex(480, 320, (c, w, h) => {
      c.fillStyle = '#071116'; c.fillRect(0, 0, w, h);
      c.fillStyle = '#54e8ef'; c.font = '700 30px system-ui,sans-serif'; c.fillText('HOME HUB', 22, 44);
      c.fillStyle = '#93aab2'; c.font = '20px system-ui,sans-serif'; c.fillText('illustrative UI', 320, 42);
      [[22, 70, 'SERVER FAN', 'API v1'], [250, 70, 'NETWORK', 'Wi-Fi'], [22, 180, 'CPU TEMP', '-- C'], [250, 180, 'FAN 1 / 2', '-- rpm']].forEach((k) => {
        c.fillStyle = '#10222b'; c.fillRect(k[0], k[1], 208, 92); c.strokeStyle = '#26424d'; c.strokeRect(k[0], k[1], 208, 92);
        c.fillStyle = '#93aab2'; c.font = '600 18px system-ui,sans-serif'; c.fillText(k[2], k[0] + 14, k[1] + 30);
        c.fillStyle = '#e8f3f5'; c.font = '700 30px system-ui,sans-serif'; c.fillText(k[3], k[0] + 14, k[1] + 68);
      });
    });
    K.box(g, [98, 1.6, 58], [0, 0.8, 0], 0x1d2a30);                                     // PCB
    const glass = K.box(g, [86, 2.2, 56], [-3, 2.7, 0], 0x0a0c0d);                     // panel body
    const face = new T.Mesh(new T.PlaneGeometry(74, 49.5), new T.MeshBasicMaterial({ map: scr })); face.rotation.x = -Math.PI / 2; face.position.set(-3, 3.85, 0); g.add(face);
    K.box(g, [8.5, 3.2, 9], [-49.5, -1.6, 0], 0xc9cdd0, { metalness: 0.8 });          // USB-C shell, left edge
    K.box(g, [24, 3, 24], [34, -1.8, 0], 0x1a1e20);                                     // ESP32 module (back)
    K.box(g, [16, 1.2, 12], [34, -2.9, 0], 0xb9bfc3, { metalness: 0.8 });
    K.box(g, [3.6, 1, 4.4], [-30, -1.3, 27], 0x1b1f22); K.box(g, [3.6, 1, 4.4], [-24, -1.3, 27], 0x1b1f22); // RST / BOOT
    [[-14, -27.4], [-6, -27.4], [2, -27.4], [10, -27.4]].forEach((p) => K.box(g, [7, 5, 3.2], [p[0] + 12, -2.4, p[1]], 0xecebe4)); // top connectors: IO35, speaker, SPI, I2C
    [[-4, 27.4], [10, 27.4], [24, 27.4]].forEach((p) => K.box(g, [7, 5, 3.2], [p[0], -2.4, p[1]], 0xecebe4));                     // bottom: BAT, UART, SD area
    K.box(g, [15, 2, 14], [-5, -1.6, 20], 0x9aa2a6, { metalness: 0.7 });                // microSD cage
    return g;
  };

  window.Parts = P;
})();
