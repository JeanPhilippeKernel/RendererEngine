#type vertex
#version 450 core

precision mediump float;

layout (location = 0) in float 	a_index;
layout (location = 1) in vec3 	a_position;
layout (location = 2) in vec4 	a_color;

layout (location = 3) in float 	a_texture_slot_id;
layout (location = 4) in vec2 	a_texture_coord;


//uniform variables
uniform mat4 uniform_viewprojection;


//output variables
out float	mesh_index;
out float 	texture_slot_id;
out vec2 	texture_coord;

void main()
{	
	gl_Position 			= uniform_viewprojection * vec4(a_position, 1.0f);
	mesh_index				= a_index;
	texture_slot_id			= a_texture_slot_id;
	texture_coord 			= a_texture_coord;
}

#type fragment
#version  450 core

precision mediump float;

in float	mesh_index;
in float 	texture_slot_id;
in vec2 	texture_coord;


uniform sampler2D	uniform_texture_slot[32];

uniform float		texture_tiling_factor_0;
uniform float		texture_tiling_factor_1;
uniform float		texture_tiling_factor_2;
uniform float		texture_tiling_factor_3;
uniform float		texture_tiling_factor_4;
uniform float		texture_tiling_factor_5;
uniform float		texture_tiling_factor_6;
uniform float		texture_tiling_factor_7;
uniform float		texture_tiling_factor_8;
uniform float		texture_tiling_factor_9;
uniform float		texture_tiling_factor_10;
uniform float		texture_tiling_factor_11;
uniform float		texture_tiling_factor_12;
uniform float		texture_tiling_factor_13;
uniform float		texture_tiling_factor_14;
uniform float		texture_tiling_factor_15;
uniform float		texture_tiling_factor_16;
uniform float		texture_tiling_factor_17;
uniform float		texture_tiling_factor_18;
uniform float		texture_tiling_factor_19;
uniform float		texture_tiling_factor_20;
uniform float		texture_tiling_factor_21;
uniform float		texture_tiling_factor_22;
uniform float		texture_tiling_factor_23;
uniform float		texture_tiling_factor_24;
uniform float		texture_tiling_factor_25;
uniform float		texture_tiling_factor_26;
uniform float		texture_tiling_factor_27;
uniform float		texture_tiling_factor_28;
uniform float		texture_tiling_factor_29;
uniform float		texture_tiling_factor_30;
uniform float		texture_tiling_factor_31;

uniform vec4		texture_tint_color_0;
uniform vec4		texture_tint_color_1;
uniform vec4		texture_tint_color_2;
uniform vec4		texture_tint_color_3;
uniform vec4		texture_tint_color_4;
uniform vec4		texture_tint_color_5;
uniform vec4		texture_tint_color_6;
uniform vec4		texture_tint_color_7;
uniform vec4		texture_tint_color_8;
uniform vec4		texture_tint_color_9;
uniform vec4		texture_tint_color_10;
uniform vec4		texture_tint_color_11;
uniform vec4		texture_tint_color_12;
uniform vec4		texture_tint_color_13;
uniform vec4		texture_tint_color_14;
uniform vec4		texture_tint_color_15;
uniform vec4		texture_tint_color_16;
uniform vec4		texture_tint_color_17;
uniform vec4		texture_tint_color_18;
uniform vec4		texture_tint_color_19;
uniform vec4		texture_tint_color_20;
uniform vec4		texture_tint_color_21;
uniform vec4		texture_tint_color_22;
uniform vec4		texture_tint_color_23;
uniform vec4		texture_tint_color_24;
uniform vec4		texture_tint_color_25;
uniform vec4		texture_tint_color_26;
uniform vec4		texture_tint_color_27;
uniform vec4		texture_tint_color_28;
uniform vec4		texture_tint_color_29;
uniform vec4		texture_tint_color_30;
uniform vec4		texture_tint_color_31;

// output variables
out vec4 output_color;

void main()
{

	vec4 color =  vec4(1.0f, 1.0f, 1.0f, 1.0f);

	switch(int(texture_slot_id)) 
	{
		case 0: color = texture(uniform_texture_slot[0], texture_coord * texture_tiling_factor_0); break;
		case 1: color = texture(uniform_texture_slot[1], texture_coord * texture_tiling_factor_1); break;
		case 2: color = texture(uniform_texture_slot[2], texture_coord * texture_tiling_factor_2); break;
		case 3: color = texture(uniform_texture_slot[3], texture_coord * texture_tiling_factor_3); break;
		case 4: color = texture(uniform_texture_slot[4], texture_coord * texture_tiling_factor_4); break;
		case 5: color = texture(uniform_texture_slot[5], texture_coord * texture_tiling_factor_5); break;
		case 6: color = texture(uniform_texture_slot[6], texture_coord * texture_tiling_factor_6); break;
		case 7: color = texture(uniform_texture_slot[7], texture_coord * texture_tiling_factor_7); break;
		case 8: color = texture(uniform_texture_slot[8], texture_coord * texture_tiling_factor_8); break;
		case 9: color = texture(uniform_texture_slot[9], texture_coord * texture_tiling_factor_9); break;
		case 10: color = texture(uniform_texture_slot[10], texture_coord * texture_tiling_factor_10); break;
		case 11: color = texture(uniform_texture_slot[11], texture_coord * texture_tiling_factor_11); break;
		case 12: color = texture(uniform_texture_slot[12], texture_coord * texture_tiling_factor_12); break;
		case 13: color = texture(uniform_texture_slot[13], texture_coord * texture_tiling_factor_13); break;
		case 14: color = texture(uniform_texture_slot[14], texture_coord * texture_tiling_factor_14); break;
		case 15: color = texture(uniform_texture_slot[15], texture_coord * texture_tiling_factor_15); break;
		case 16: color = texture(uniform_texture_slot[16], texture_coord * texture_tiling_factor_16); break;
		case 17: color = texture(uniform_texture_slot[17], texture_coord * texture_tiling_factor_17); break;
		case 18: color = texture(uniform_texture_slot[18], texture_coord * texture_tiling_factor_18); break;
		case 19: color = texture(uniform_texture_slot[19], texture_coord * texture_tiling_factor_19); break;
		case 20: color = texture(uniform_texture_slot[20], texture_coord * texture_tiling_factor_20); break;
		case 21: color = texture(uniform_texture_slot[21], texture_coord * texture_tiling_factor_21); break;
		case 22: color = texture(uniform_texture_slot[22], texture_coord * texture_tiling_factor_22); break;
		case 23: color = texture(uniform_texture_slot[23], texture_coord * texture_tiling_factor_23); break;
		case 24: color = texture(uniform_texture_slot[24], texture_coord * texture_tiling_factor_24); break;
		case 25: color = texture(uniform_texture_slot[25], texture_coord * texture_tiling_factor_25); break;
		case 26: color = texture(uniform_texture_slot[26], texture_coord * texture_tiling_factor_26); break;
		case 27: color = texture(uniform_texture_slot[27], texture_coord * texture_tiling_factor_27); break;
		case 28: color = texture(uniform_texture_slot[28], texture_coord * texture_tiling_factor_28); break;
		case 29: color = texture(uniform_texture_slot[29], texture_coord * texture_tiling_factor_29); break;
		case 30: color = texture(uniform_texture_slot[30], texture_coord * texture_tiling_factor_30); break;
		case 31: color = texture(uniform_texture_slot[31], texture_coord * texture_tiling_factor_31); break;
	}


	switch(int(mesh_index)) 
	{
		case 0:		color = color * texture_tint_color_0; break; 
		case 1: 	color = color * texture_tint_color_1; break;
		case 2: 	color = color * texture_tint_color_2; break;
		case 3: 	color = color * texture_tint_color_3; break;
		case 4: 	color = color * texture_tint_color_4; break;
		case 5: 	color = color * texture_tint_color_5; break;
		case 6: 	color = color * texture_tint_color_6; break;
		case 7: 	color = color * texture_tint_color_7; break;
		case 8: 	color = color * texture_tint_color_8; break;
		case 9: 	color = color * texture_tint_color_9; break;
		case 10:	color = color * texture_tint_color_10; break;
		case 11:	color = color * texture_tint_color_11; break;
		case 12:	color = color * texture_tint_color_12; break;
		case 13:	color = color * texture_tint_color_13; break;
		case 14:	color = color * texture_tint_color_14; break;
		case 15:	color = color * texture_tint_color_15; break;
		case 16:	color = color * texture_tint_color_16; break;
		case 17:	color = color * texture_tint_color_17; break;
		case 18:	color = color * texture_tint_color_18; break;
		case 19:	color = color * texture_tint_color_19; break;
		case 20:	color = color * texture_tint_color_20; break;
		case 21:	color = color * texture_tint_color_21; break;
		case 22:	color = color * texture_tint_color_22; break;
		case 23:	color = color * texture_tint_color_23; break;
		case 24:	color = color * texture_tint_color_24; break;
		case 25:	color = color * texture_tint_color_25; break;
		case 26:	color = color * texture_tint_color_26; break;
		case 27:	color = color * texture_tint_color_27; break;
		case 28:	color = color * texture_tint_color_28; break;
		case 29:	color = color * texture_tint_color_29; break;
		case 30:	color = color * texture_tint_color_30; break;
		case 31:	color = color * texture_tint_color_31; break;
	}

	output_color = color;

}



 
