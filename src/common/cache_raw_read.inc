static uint32_t read_cache_rawfile(uint32_t offset)
{
	int16_t new_block = offset >> BLOCK_SHIFT;
	uint32_t idx = blocks[new_block];
	cache_t *p;
	size_t read_size = cache_resolved_read_size();
	unsigned int part = (offset & BLOCK_MASK) / (unsigned int)read_size;
	unsigned int within = part * (unsigned int)read_size;
	unsigned int first_quarter = within / CACHE_READ_QUARTER_SIZE;
	unsigned int quarter_count = (unsigned int)read_size / CACHE_READ_QUARTER_SIZE;
	uint8_t valid_mask = (uint8_t)(((1u << quarter_count) - 1u) << first_quarter);

	if (idx == BLOCK_NOT_CACHED)
	{
		p = head;
		if (p->block >= 0)
			blocks[p->block] = BLOCK_NOT_CACHED;
		p->block = new_block;
		blocks[new_block] = p->idx;
		gfx_valid_parts[p->idx] = 0;
	}
	else
		p = &cache_data[idx];

	if ((gfx_valid_parts[p->idx] & valid_mask) != valid_mask)
	{
		uint8_t *dst = &GFX_MEMORY[(p->idx << BLOCK_SHIFT) + within];
#if (EMU_SYSTEM == MVS)
#ifdef CACHE_IO_PROFILE
		cache_io_profile_miss(&crom_io_profile,
			new_block * (CACHE_BLOCK_SIZE / (unsigned int)read_size) + part);
#endif
		if (mvs_cache_read_range(cache_fd, &cache_storage_handle, &cache_file_pos,
			new_block, dst, "crom", within, read_size
#ifdef CACHE_IO_PROFILE
			, &crom_io_profile, 1
#endif
		))
			gfx_valid_parts[p->idx] |= valid_mask;
		else
			memset(dst, 0, read_size);
#else
		if (lseek((int32_t)cache_fd, (off_t)block_offset[new_block] + within, SEEK_SET) >= 0)
		{
			ssize_t bytes = read((int32_t)cache_fd, dst, read_size);
			if (bytes == (ssize_t)read_size)
				gfx_valid_parts[p->idx] |= valid_mask;
			else
				memset(dst, 0, read_size);
		}
		else
			memset(dst, 0, read_size);
#endif
	}
#ifdef CACHE_IO_PROFILE
	else
		crom_io_profile.hits++;
#endif

	if (p->next)
	{
		if (p->prev)
		{
			p->prev->next = p->next;
			p->next->prev = p->prev;
		}
		else
		{
			head = p->next;
			head->prev = NULL;
		}
		p->prev = tail;
		p->next = NULL;
		tail->next = p;
		tail = p;
	}

	return ((uint32_t)p->idx << BLOCK_SHIFT) | (offset & BLOCK_MASK);
}
