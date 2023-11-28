float log10(float x)
{
    return log(x) / log(10.0);
}

float satf(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec2 satv(vec2 x)
{
    return clamp(x, vec2(0.0), vec2(1.0));
}

float max2(vec2 v)
{
    return max(v.x, v.y);
}