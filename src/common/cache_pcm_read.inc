uint8_t *pcm_cache_read(uint16_t new_part)
{
	const unsigned int requested_quarter = new_part & 3u;
	const uint16_t new_block = new_part >> 2;
	const size_t read_size = cache_resolved_read_size();
	const unsigned int quarters_per_read = (unsigned int)read_size / CACHE_READ_QUARTER_SIZE;
	const unsigned int first_quarter = (requested_quarter / quarters_per_read) * quarters_per_read;
	const unsigned int within = first_quarter * CACHE_READ_QUARTER_SIZE;
	const uint8_t valid_mask = (uint8_t)(((1u << quarters_per_read) - 1u) << first_quarter);
	uint32_t idx = pcm_blocks[new_block];
	cache_t *p;

	if (idx == BLOCK_NOT_CACHED)
	{
		p = pcm_head;
		if (p->block >= 0)
			pcm_blocks[p->block] = BLOCK_NOT_CACHED;
		p->block = new_block;
		pcm_blocks[new_block] = p->idx;
		pcm_valid_parts[p->idx] = 0;
	}
	else
		p = &pcm_data[idx];

	if ((pcm_valid_parts[p->idx] & valid_mask) != valid_mask)
	{
#ifdef CACHE_IO_PROFILE
		cache_io_profile_miss(&pcm_io_profile,
			new_block * (CACHE_BLOCK_SIZE / (unsigned int)read_size) +
			first_quarter / quarters_per_read);
#endif
		uint8_t *dst = &memory_region_sound1[(p->idx << BLOCK_SHIFT) + within];
		if (mvs_cache_read_range(pcm_fd, &pcm_storage_handle, &pcm_file_pos,
			new_block, dst, "pcm", within, read_size
#ifdef CACHE_IO_PROFILE
			, &pcm_io_profile, 1
#endif
		))
			pcm_valid_parts[p->idx] |= valid_mask;
		else
			memset(dst, 0, read_size);
	}
#ifdef CACHE_IO_PROFILE
	else
		pcm_io_profile.hits++;
#endif

	if (p->frame != frames_displayed)
	{
		p->frame = frames_displayed;
		if (p->next)
		{
			if (p->prev)
			{
				p->prev->next = p->next;
				p->next->prev = p->prev;
			}
			else
			{
				pcm_head = p->next;
				pcm_head->prev = NULL;
			}
			p->prev = pcm_tail;
			p->next = NULL;
			pcm_tail->next = p;
			pcm_tail = p;
		}
	}

	return &memory_region_sound1[(p->idx << BLOCK_SHIFT) +
		requested_quarter * CACHE_READ_QUARTER_SIZE];
}
