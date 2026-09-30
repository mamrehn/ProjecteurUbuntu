// SPDX-License-Identifier: MIT
import Cogl from 'gi://Cogl';
import GObject from 'gi://GObject';
import Shell from 'gi://Shell';

/**
 * Multiplies an actor's pixels by a soft-edged circular mask.
 *
 *  keepInside = true : circle stays, everything outside becomes transparent (a round lens)
 *  keepInside = false: circle becomes transparent, everything outside stays (a spotlight hole)
 *
 * Positions and sizes are in the actor's own pixels.
 *
 * IMPORTANT (learned the hard way, it segfaults libmutter-cogl): the effect's final Cogl pipeline
 * does not exist while vfunc_build_pipeline() runs. get_uniform_location()/set_uniform_float() may
 * only be called from vfunc_paint_target() or later, never from the constructor/build_pipeline.
 */
export const CircleMaskEffect = GObject.registerClass(
class CircleMaskEffect extends Shell.GLSLEffect {
    _init(keepInside) {
        super._init();
        this._keepInside = keepInside ? 1.0 : 0.0;
        this._center = [0, 0];
        this._radius = 1;
        this._feather = 1.5;
        this._size = [1, 1];
        this._loc = null;
    }

    vfunc_build_pipeline() {
        const declarations = `
            uniform vec2 pj_center;
            uniform float pj_radius;
            uniform float pj_feather;
            uniform vec2 pj_size;
            uniform float pj_keep_inside;`;
        const code = `
            vec2 pj_pos = cogl_tex_coord_in[0].xy * pj_size;
            float pj_d = distance(pj_pos, pj_center);
            float pj_in = 1.0 - smoothstep(pj_radius - pj_feather, pj_radius, pj_d);
            float pj_mask = mix(1.0 - pj_in, pj_in, pj_keep_inside);
            cogl_color_out *= pj_mask;`;
        this.add_glsl_snippet(Cogl.SnippetHook.FRAGMENT, declarations, code, false);
    }

    vfunc_paint_target(node, paintContext) {
        this._applyUniforms();
        super.vfunc_paint_target(node, paintContext);
    }

    _applyUniforms() {
        if (!this._loc) {
            this._loc = {
                center: this.get_uniform_location('pj_center'),
                radius: this.get_uniform_location('pj_radius'),
                feather: this.get_uniform_location('pj_feather'),
                size: this.get_uniform_location('pj_size'),
                keep: this.get_uniform_location('pj_keep_inside'),
            };
        }
        this.set_uniform_float(this._loc.center, 2, this._center);
        this.set_uniform_float(this._loc.radius, 1, [this._radius]);
        this.set_uniform_float(this._loc.feather, 1, [this._feather]);
        this.set_uniform_float(this._loc.size, 2, this._size);
        this.set_uniform_float(this._loc.keep, 1, [this._keepInside]);
    }

    /** @param {number[]} center @param {number} radius @param {number[]} size @param {number} [feather] */
    setCircle(center, radius, size, feather = 1.5) {
        this._center = center;
        this._radius = radius;
        this._size = size;
        this._feather = feather;
        this.queue_repaint();
    }
});
