/* Minecraft 1.7.10 AxisAlignedBB, by value. Java allocates a new box in
 * addCoord/expand/getOffsetBoundingBox/copy and mutates in place in offset/
 * setBB; the C side carries the struct and returns the new box, so every call
 * site names the same six numbers the Java object holds at that point.
 *
 * The offset rules are the ones Entity.moveEntity drives: a list box clips
 * the entity's motion only when the two boxes overlap on the other two axes,
 * and only the box that is first along the motion's direction can shorten
 * it. The functions are inline (lane/mobtick): the movement and the box scans
 * called them a few times a cell, each a call returning six doubles through
 * memory; the same operations in the same order (-ffp-contract=off). */
#ifndef NETHERITE_AABB_H
#define NETHERITE_AABB_H

struct aabb {
    double min_x, min_y, min_z, max_x, max_y, max_z;
};

/* getBoundingBox / the constructor. */
static inline struct aabb aabb_make(double min_x, double min_y, double min_z, double max_x, double max_y, double max_z)
{
    struct aabb b = {min_x, min_y, min_z, max_x, max_y, max_z};
    return b;
}

/* AxisAlignedBB.copy(). */
static inline struct aabb aabb_copy(struct aabb b)
{
    return b;
}

/* addCoord: extends toward the sign of each delta only. */
static inline struct aabb aabb_add_coord(struct aabb b, double dx, double dy, double dz)
{
    if (dx < 0.0) b.min_x += dx;
    if (dx > 0.0) b.max_x += dx;
    if (dy < 0.0) b.min_y += dy;
    if (dy > 0.0) b.max_y += dy;
    if (dz < 0.0) b.min_z += dz;
    if (dz > 0.0) b.max_z += dz;
    return b;
}

/* expand: min shrinks by the delta, max grows by it (negative shrinks). */
static inline struct aabb aabb_expand(struct aabb b, double dx, double dy, double dz)
{
    return aabb_make(b.min_x - dx, b.min_y - dy, b.min_z - dz, b.max_x + dx, b.max_y + dy, b.max_z + dz);
}

/* getOffsetBoundingBox: a pure translation, both ends move together. */
static inline struct aabb aabb_offset_box(struct aabb b, double dx, double dy, double dz)
{
    return aabb_make(b.min_x + dx, b.min_y + dy, b.min_z + dz, b.max_x + dx, b.max_y + dy, b.max_z + dz);
}

/* offset: in place, returns the same struct for chaining. */
static inline struct aabb aabb_offset(struct aabb b, double dx, double dy, double dz)
{
    b.min_x += dx;
    b.min_y += dy;
    b.min_z += dz;
    b.max_x += dx;
    b.max_y += dy;
    b.max_z += dz;
    return b;
}

/* intersectsWith. */
static inline int aabb_intersects(const struct aabb *a, const struct aabb *b)
{
    return b->max_x > a->min_x && b->min_x < a->max_x && b->max_y > a->min_y && b->min_y < a->max_y &&
           b->max_z > a->min_z && b->min_z < a->max_z;
}

/* calculateX/Y/ZOffset. Java calls them on a box from the colliding list with
 * the entity's box as the argument, so the first parameter is the list box. */
static inline double aabb_calculate_x_offset(const struct aabb *box, const struct aabb *entity, double dx)
{
    if (!(entity->max_y > box->min_y && entity->min_y < box->max_y)) return dx;
    if (!(entity->max_z > box->min_z && entity->min_z < box->max_z)) return dx;

    double d;

    if (dx > 0.0 && entity->max_x <= box->min_x)
    {
        d = box->min_x - entity->max_x;
        if (d < dx) dx = d;
    }

    if (dx < 0.0 && entity->min_x >= box->max_x)
    {
        d = box->max_x - entity->min_x;
        if (d > dx) dx = d;
    }

    return dx;
}

static inline double aabb_calculate_y_offset(const struct aabb *box, const struct aabb *entity, double dy)
{
    if (!(entity->max_x > box->min_x && entity->min_x < box->max_x)) return dy;
    if (!(entity->max_z > box->min_z && entity->min_z < box->max_z)) return dy;

    double d;

    if (dy > 0.0 && entity->max_y <= box->min_y)
    {
        d = box->min_y - entity->max_y;
        if (d < dy) dy = d;
    }

    if (dy < 0.0 && entity->min_y >= box->max_y)
    {
        d = box->max_y - entity->min_y;
        if (d > dy) dy = d;
    }

    return dy;
}

static inline double aabb_calculate_z_offset(const struct aabb *box, const struct aabb *entity, double dz)
{
    if (!(entity->max_x > box->min_x && entity->min_x < box->max_x)) return dz;
    if (!(entity->max_y > box->min_y && entity->min_y < box->max_y)) return dz;

    double d;

    if (dz > 0.0 && entity->max_z <= box->min_z)
    {
        d = box->min_z - entity->max_z;
        if (d < dz) dz = d;
    }

    if (dz < 0.0 && entity->min_z >= box->max_z)
    {
        d = box->max_z - entity->min_z;
        if (d > dz) dz = d;
    }

    return dz;
}

#endif
