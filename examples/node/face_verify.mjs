#!/usr/bin/env node
// Face verification: are these two images the same person?
//
// Usage:
//     node examples/node/face_verify.mjs <image_a> <image_b>
//     node examples/node/face_verify.mjs --threshold 0.30 alice.jpg bob.jpg
//
// Requires `sharp` to decode images:  npm install sharp

import { Engine, similarity } from 'naina';
import sharp from 'sharp';
import { argv, exit } from 'node:process';

async function loadRgb(path) {
    const { data, info } = await sharp(path)
        .removeAlpha()
        .toColorspace('srgb')
        .raw()
        .toBuffer({ resolveWithObject: true });
    return {
        data: new Uint8Array(data.buffer, data.byteOffset, data.byteLength),
        width: info.width,
        height: info.height,
        channels: 3,
        format: 'rgb',
    };
}

function bestFace(faces) {
    if (faces.length === 0) return null;
    return faces.reduce((a, b) => (b.bbox.score > a.bbox.score ? b : a));
}

async function main() {
    const args = argv.slice(2);
    let threshold = 0.36;
    const positional = [];
    for (let i = 0; i < args.length; ++i) {
        if (args[i] === '--threshold') threshold = parseFloat(args[++i]);
        else positional.push(args[i]);
    }
    if (positional.length !== 2) {
        console.error('usage: face_verify.mjs <image_a> <image_b> [--threshold N]');
        exit(2);
    }

    const engine = new Engine();
    const [a, b] = positional;
    const imgA = await loadRgb(a);
    const imgB = await loadRgb(b);

    const facesA = await engine.detectFaces(imgA);
    const facesB = await engine.detectFaces(imgB);
    console.log(`${a}: ${facesA.length} face(s)`);
    console.log(`${b}: ${facesB.length} face(s)`);

    const faceA = bestFace(facesA);
    const faceB = bestFace(facesB);
    if (!faceA || !faceB) {
        console.error('no face detected in one of the inputs');
        exit(1);
    }

    const embA = await engine.embedFace(imgA, faceA);
    const embB = await engine.embedFace(imgB, faceB);
    const sim = similarity(embA, embB);

    const verdict = sim >= threshold ? 'SAME PERSON' : 'different people';
    console.log();
    console.log(`cosine similarity: ${sim.toFixed(4)}`);
    console.log(`threshold:         ${threshold.toFixed(4)}`);
    console.log(`verdict:           ${verdict}`);
}

await main();
