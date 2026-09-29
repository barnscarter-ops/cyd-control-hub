/* Control Hub - proposed working assembly: CYD on carrier PCB inside printed bezel/backplate. Rendered proposal only. */
(function () {
  const T = THREE, K = Kit, P = Parts;
  const ctx = K.boot({
    components: window.DIAGRAM_COMPONENTS,
    camera: { pos: [90, 110, 150], target: [0, 8, 0] },
    shadowExtent: 240,
    absentNote: 'Not drawn in the assembly view: it is a separate device or a bench item.',
    tiers: {
      'CH-01': [['photo', 'photo-confirmed board'], ['owner', 'owner-reported quantity']],
      'CH-02': [['proposed', 'proposed - inspect supply and lead']],
      'CH-04': [['proposed', 'proposed board - obtain required']],
      'CH-05': [['proposed', 'proposed printed part - rendered only']],
    },
    nets: [
      ['USB power', 'CH-02 5 V lead → CH-01 USB-C through the backplate service opening', 'v5', 'CH-02'],
      ['Mounting', 'CH-05 M3 standoffs → CH-04 carrier → CH-01 mounting holes', 'net', 'CH-05'],
      ['Service headers', 'CH-04 keyed low-voltage headers (I2C, SPI) - no fan or load power', 'sig', 'CH-04'],
    ],
  });
  const scene = ctx.scene;
  const gPrint = new T.Group(), gExplode = new T.Group();
  scene.add(gPrint, gExplode);
  ctx.addLayer('print', 'Bezel / backplate (CH-05)', gPrint, true);
  ctx.addView('Assembly', [90, 110, 150], [0, 8, 0]);
  ctx.addView('Top', [0, 260, 1], [0, 0, 0]);
  ctx.addView('Side', [0, 30, 230], [0, 14, 0]);
  ctx.addView('Underside', [70, -110, 130], [0, 10, 0]);
  const lbl = (t, x, y, z, s, o) => { const l = K.label(t, Object.assign({ size: s || 5 }, o)); l.position.set(x, y, z); ctx.labels.add(l); return l; };

  const bench = new T.Mesh(new T.PlaneGeometry(1400, 1000), K.mat(0x172832, { roughness: 0.9 }));
  bench.rotation.x = -Math.PI / 2; bench.position.y = -0.4; bench.receiveShadow = true; scene.add(bench);
  const grid = new T.GridHelper(1000, 50, 0x2c4553, 0x213641); grid.position.y = -0.3; grid.material.transparent = true; grid.material.opacity = 0.5; scene.add(grid);
  const ghost = (obj, col, op) => obj.traverse((o) => { if (o.isMesh) { o.material = new T.MeshStandardMaterial({ color: col, transparent: true, opacity: op, roughness: 0.4, depthWrite: false }); o.material.userData.ghost = true; o.castShadow = false; } });

  // stack (mm): backplate 0..3, carrier standoffs 3..11 (carrier y 11..12.6), CYD standoffs 12.6..22 (board y 22)
  const CAR = { w: 106, d: 66 }, CARY = 11;
  const carTex = K.tex(CAR.w * 8, CAR.d * 8, (c, w, h) => {
    const X = (x) => (x + CAR.w / 2) * 8, Z = (z) => (z + CAR.d / 2) * 8;
    c.fillStyle = '#0d5a3a'; c.fillRect(0, 0, w, h);
    c.strokeStyle = '#d9c08a'; c.lineWidth = 6; c.lineCap = 'round'; c.lineJoin = 'round';
    c.beginPath(); c.moveTo(X(-38), Z(-22)); c.lineTo(X(-38), Z(-10)); c.lineTo(X(-20), Z(-10)); c.stroke();
    c.beginPath(); c.moveTo(X(-34), Z(-22)); c.lineTo(X(-34), Z(-14)); c.lineTo(X(-20), Z(-14)); c.stroke();
    c.beginPath(); c.moveTo(X(-30), Z(-22)); c.lineTo(X(-30), Z(-18)); c.lineTo(X(-20), Z(-18)); c.stroke();
    c.beginPath(); c.moveTo(X(10), Z(-22)); c.lineTo(X(10), Z(0)); c.lineTo(X(30), Z(0)); c.stroke();
    c.strokeStyle = '#eef3ea'; c.lineWidth = 2; c.strokeRect(X(-CAR.w / 2 + 2), Z(-CAR.d / 2 + 2), (CAR.w - 4) * 8, (CAR.d - 4) * 8);
    c.fillStyle = '#eef3ea'; c.font = '600 22px system-ui,sans-serif'; c.fillText('CH-04  CYD carrier  PROPOSED - not fabricated', X(-46), Z(CAR.d / 2 - 5));
    c.font = '600 16px system-ui,sans-serif'; c.fillText('I2C', X(-42), Z(-25)); c.fillText('SPI', X(-24), Z(-25)); c.fillText('LOW VOLTAGE ONLY', X(-46), Z(-8));
    [[-49, -29], [49, -29], [-49, 29], [49, 29]].forEach((p) => { c.fillStyle = '#d9c08a'; c.beginPath(); c.arc(X(p[0]), Z(p[1]), 3.4 * 8, 0, 7); c.fill(); c.fillStyle = '#071017'; c.beginPath(); c.arc(X(p[0]), Z(p[1]), 1.6 * 8, 0, 7); c.fill(); });
  });
  const cm = [0, 1, 2, 3, 4, 5].map(() => K.mat(0x0d5a3a)); cm[2] = new T.MeshStandardMaterial({ map: carTex, roughness: 0.55 });
  const car = K.box(scene, [CAR.w, 1.6, CAR.d], [0, CARY + 0.8, 0], cm[0]); car.material = cm; ctx.reg('CH-04', car);
  lbl('CH-04  carrier PCB', 0, CARY + 14, 34, 5);
  // keyed service headers on carrier (rotated toward the near edge)
  [-40, -24].forEach((x) => {
    const h = new T.Group(); K.box(h, [10, 5, 6], [0, 2.5, 0], 0x14181b); K.box(h, [8, 2, 2], [0, 4.5, 2.3], 0x14181b);
    for (let i = 0; i < 4; i++) K.box(h, [0.7, 4, 0.7], [-3.8 + i * 2.54, 7, 0], 0xd8c27a, { metalness: 0.7 });
    h.position.set(x, CARY + 1.6, -22); scene.add(h); ctx.reg('CH-04', h);
  });

  // M3 standoffs: backplate->carrier (brass) and carrier->CYD
  [[-49, -29], [49, -29], [-49, 29], [49, 29]].forEach((p) => {
    const lo = K.cyl(scene, 3, CARY - 3, [p[0], 3 + (CARY - 3) / 2, p[1]], 0xc9a24a, { m: { metalness: 0.7 }, seg: 6 }); ctx.reg('CH-05', lo);
    const hi = K.cyl(scene, 3, 22 - CARY - 1.6, [p[0], CARY + 1.6 + (22 - CARY - 1.6) / 2, p[1]], 0xc9a24a, { m: { metalness: 0.7 }, seg: 6 }); ctx.reg('CH-05', hi);
    const sc = K.cyl(scene, 2.6, 1.6, [p[0], 22 + 1.8, p[1]], 0x8a9196, { m: { metalness: 0.8 } }); ctx.reg('CH-05', sc);
  });

  // CH-01 on top
  const cyd = P.cyd(); cyd.position.set(0, 22, 0); scene.add(cyd); ctx.reg('CH-01', cyd);
  lbl('CH-01  CYD', 0, 34, 0, 6);

  // CH-05 backplate (ghost) with USB-C service cutout on the left edge; bezel frame around the screen
  const back = new T.Group();
  const bp = new T.Mesh(new T.BoxGeometry(CAR.w + 8, 3, CAR.d + 8), new T.MeshStandardMaterial()); bp.position.set(0, 1.5, 0); back.add(bp);
  [[-1, 0], [1, 0], [0, -1], [0, 1]].forEach((d) => {
    const w = d[0] ? 3 : CAR.w + 8, dd = d[1] ? 3 : CAR.d + 8;
    const wall = new T.Mesh(new T.BoxGeometry(w, 9, dd), new T.MeshStandardMaterial()); wall.position.set(d[0] * (CAR.w + 5) / 2, 7.5, d[1] * (CAR.d + 5) / 2); back.add(wall);
  });
  ghost(back, 0x88a4b5, 0.2);
  const bez = new T.Group();
  const bw = 106, bd = 66, bt = 9;
  [[0, -1], [0, 1]].forEach((d) => { const m = new T.Mesh(new T.BoxGeometry(bw + 8, 5, bt), new T.MeshStandardMaterial()); m.position.set(0, 25.5, d[1] * (bd / 2 + 0.0 - 0.0) * (bd - 48) / bd + d[1] * 20 + d[1] * 2); bez.add(m); });
  [[-1], [1]].forEach((d) => { const m = new T.Mesh(new T.BoxGeometry(bt - 2, 5, bd + 8), new T.MeshStandardMaterial()); m.position.set(d[0] * (bw / 2 - 1), 25.5, 0); bez.add(m); });
  ghost(bez, 0xc58bff, 0.22);
  gPrint.add(back, bez);
  back.children.concat(bez.children).forEach((o) => { o.userData.pid = 'CH-05'; });
  ctx.reg('CH-05', back); ctx.reg('CH-05', bez);
  lbl('CH-05  backplate + bezel (printed)', -50, 4, 44, 4.6, { border: '#c58bff' });
  const cut = K.box(scene, [5, 0.4, 11], [-56, 3.2, 0], 0x0a0c0d); cut.material.userData.ghost = true;
  lbl('USB-C service opening', -68, 14, 0, 4, { border: '#ff6b9d' });

  // CH-02 lead entering the opening
  const lead = K.tube(scene, [[-56, 20, 0], [-64, 18, 0], [-90, 12, 14], [-130, 6, 30]], 0x1b1f22, 2.1, { tension: 0.4 }); ctx.reg('CH-02', lead);
  K.box(scene, [12, 6, 9], [-52, 22, 0], 0x2a2f31);
  ctx.finish();
  ctx.select('CH-04');
})();
