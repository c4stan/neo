struct reservoir_t {
    uint light;
    float weight;
    float weight_sum;
    float light_count;
    uint debug;
};

bool update_reservoir ( inout reservoir_t res, uint light, float weight, float count, inout rng_wang_state_t rng_state ) {
    res.weight_sum += weight;
    res.light_count += count;

    float e = rng_wang ( rng_state );
    if ( e < weight / res.weight_sum ) {
        res.light = light;
        return true;
    }

    return false;
}

reservoir_t reservoir_init ( void ) {
    return reservoir_t ( 0, 0, 0, 0, 0 );
}
