#include <nds/system.h>
#include <nds/bios.h>
#include <nds/arm7/sdmmc.h>
#include <nds/interrupts.h>
#include <nds/fifocommon.h>
#include <nds/fifomessages.h>

#include <stddef.h>

static struct mmcdevice deviceSD;
static struct mmcdevice deviceNAND;

/*mmcdevice *getMMCDevice(int drive) {
    if(drive==0) return &deviceNAND;
    return &deviceSD;
}
*/

//---------------------------------------------------------------------------------
int geterror(struct mmcdevice *ctx) {
//---------------------------------------------------------------------------------
    //if(ctx->error == 0x4) return -1;
    //else return 0;
    return (ctx->error << 29) >> 31;
}


//---------------------------------------------------------------------------------
void setTarget(struct mmcdevice *ctx) {
//---------------------------------------------------------------------------------
    sdmmc_mask16(REG_SDPORTSEL,0x3,(u16)ctx->devicenumber);
    setckl(ctx->clk);
    if (ctx->SDOPT == 0) {
        sdmmc_mask16(REG_SDOPT, 0, 0x8000);
    } else {
        sdmmc_mask16(REG_SDOPT, 0x8000, 0);
    }

}


//---------------------------------------------------------------------------------
void sdmmc_send_command(struct mmcdevice *ctx, uint32_t cmd, uint32_t args) {
//---------------------------------------------------------------------------------
	const bool getSDRESP = (cmd << 15) >> 31;
	u16 flags = (cmd << 15) >> 31;
	const bool readdata = cmd & 0x20000;
	const bool writedata = cmd & 0x40000;

	if(readdata || writedata)
	{
		flags |= TMIO_STAT0_DATAEND;
	}

	ctx->error = 0;
	while((sdmmc_read16(REG_SDSTATUS1) & TMIO_STAT1_CMD_BUSY)); //mmc working?
	sdmmc_write16(REG_SDIRMASK0,0);
	sdmmc_write16(REG_SDIRMASK1,0);
	sdmmc_write16(REG_SDSTATUS0,0);
	sdmmc_write16(REG_SDSTATUS1,0);
	sdmmc_mask16(REG_SDDATACTL32,0x1800,0x400); // Disable TX32RQ and RX32RDY IRQ. Clear fifo.
	sdmmc_write16(REG_SDCMDARG0,args &0xFFFF);
	sdmmc_write16(REG_SDCMDARG1,args >> 16);
	sdmmc_write16(REG_SDCMD,cmd &0xFFFF);

	u32 size = ctx->size;
	const u16 blkSize = sdmmc_read16(REG_SDBLKLEN32);
	u32 *rDataPtr32 = (u32*)ctx->rData;
	u8  *rDataPtr8  = ctx->rData;
	const u32 *tDataPtr32 = (u32*)ctx->tData;
	const u8  *tDataPtr8  = ctx->tData;

	bool rUseBuf = ( NULL != rDataPtr32 );
	bool tUseBuf = ( NULL != tDataPtr32 );

	u16 status0 = 0;
	while(1)
	{
		volatile u16 status1 = sdmmc_read16(REG_SDSTATUS1);
#ifdef DATA32_SUPPORT
		volatile u16 ctl32 = sdmmc_read16(REG_SDDATACTL32);
		if((ctl32 & 0x100))
#else
		if((status1 & TMIO_STAT1_RXRDY))
#endif
		{
			if(readdata)
			{
				if(rUseBuf)
				{
					sdmmc_mask16(REG_SDSTATUS1, TMIO_STAT1_RXRDY, 0);
					if(size >= blkSize)
					{
						#ifdef DATA32_SUPPORT
                        // skip startOffset bytes at the beggining of the read
                        if(ctx->startOffset>0)
                        {
                            u32 skipped=0;
                            for(u32 skipped = 0; skipped < ctx->startOffset; skipped += 4)
                            {
                                sdmmc_read32(REG_SDFIFO32);        
                            }
                            u32 remain = ctx->startOffset-skipped;
                            if(remain>0)
                            {
                                u32 data = sdmmc_read32(REG_SDFIFO32);
                                u8 data8[4];
                                u8* pdata8 = data8;
                                *pdata8++ = data;
    							*pdata8++ = data >> 8;
    							*pdata8++ = data >> 16;
    							*pdata8++ = data >> 24;
    							pdata8 = data8;
                                for (int i=0; i<remain; i++)
                                {
                                    pdata8++;        
                                }
                                for (int i=0; i<4-remain; i++)
                                {
                                    *rDataPtr8++ = *pdata8++;
                                }     
                            }
                        }
                        // copy data
                        u32 copied = 0;
						if(!((u32)rDataPtr32 & 3))
						{
							for(int i = 0; i < blkSize-ctx->startOffset-ctx->endOffset; i += 4)
							{
								*rDataPtr32++ = sdmmc_read32(REG_SDFIFO32);
								copied+=4;
							}
						}
						else
						{
							for(int i = 0; i < blkSize-ctx->startOffset-ctx->endOffset; i += 4)
							{
								u32 data = sdmmc_read32(REG_SDFIFO32);
								*rDataPtr8++ = data;
								*rDataPtr8++ = data >> 8;
								*rDataPtr8++ = data >> 16;
								*rDataPtr8++ = data >> 24;
								copied+=4;
							}
						}
                        // skip endOffset bytes at the end of the read
                        if(ctx->endOffset>0)
                        {
                          u32 remain = blkSize-ctx->startOffset-ctx->endOffset-copied;
                          if(remain)
                          {
                                u32 data = sdmmc_read32(REG_SDFIFO32);
                                u8 data8[4];
                                u8* pdata8 = data8;
                                *pdata8++ = data;
                                *pdata8++ = data >> 8;
                                *pdata8++ = data >> 16;
                                *pdata8++ = data >> 24;
                                pdata8 = data8;
                                for (int i=0; i<remain; i++)
                                {
                                    *rDataPtr8++ = *pdata8++;
                                }                                
                          }                          
                          for(u32 skipped=4-remain; skipped < ctx->endOffset; skipped += 4)
                          {
                              sdmmc_read32(REG_SDFIFO32);        
                          }
                        }
						#else
                        // startOffset & endOffset NOT IMPLEMENTED in DATA16 mode
                        // copy data : this code seems wrong
						if(!((u32)rDataPtr16 & 1))
						{
							for(u32 i = 0; i < blkSize; i += 4)
							{
								*rDataPtr16++ = sdmmc_read16(REG_SDFIFO);
							}
						}
						else
						{
							for(u32 i = 0; i < blkSize; i += 4)
							{
								u16 data = sdmmc_read16(REG_SDFIFO);
								*rDataPtr8++ = data;
								*rDataPtr8++ = data >> 8;
							}
						}
						#endif
						size -= blkSize;
					}
				}

				sdmmc_mask16(REG_SDDATACTL32, 0x800, 0);
			}
		}
#ifdef DATA32_SUPPORT
		if(!(ctl32 & 0x200))
#else
		if((status1 & TMIO_STAT1_TXRQ))
#endif
		{
			if(writedata)
			{
				if(tUseBuf)
				{
					sdmmc_mask16(REG_SDSTATUS1, TMIO_STAT1_TXRQ, 0);
					if(size >= blkSize)
					{
						#ifdef DATA32_SUPPORT
						if(!((u32)tDataPtr32 & 3))
						{
							for(u32 i = 0; i < blkSize; i += 4)
							{
								sdmmc_write32(REG_SDFIFO32, *tDataPtr32++);
							}
						}
						else
						{
							for(u32 i = 0; i < blkSize; i += 4)
							{
								u32 data = *tDataPtr8++;
								data |= (u32)*tDataPtr8++ << 8;
								data |= (u32)*tDataPtr8++ << 16;
								data |= (u32)*tDataPtr8++ << 24;
								sdmmc_write32(REG_SDFIFO32, data);
							}
						}
						#else
						if(!((u32)tDataPtr16 & 1))
						{
							for(u32 i = 0; i < blkSize; i += 2)
							{
								sdmmc_write16(REG_SDFIFO, *tDataPtr16++);
							}
						}
						else
						{
							for(u32 i = 0; i < blkSize; i += 2)
							{
								u16 data = *tDataPtr8++;
								data |= (u16)(*tDataPtr8++ << 8);
								sdmmc_write16(REG_SDFIFO, data);
							}
						}
						#endif
						size -= blkSize;
					}
				}

				sdmmc_mask16(REG_SDDATACTL32, 0x1000, 0);
			}
		}
		if(status1 & TMIO_MASK_GW)
		{
			ctx->error |= 4;
			break;
		}

		if(!(status1 & TMIO_STAT1_CMD_BUSY))
		{
			status0 = sdmmc_read16(REG_SDSTATUS0);
			if(sdmmc_read16(REG_SDSTATUS0) & TMIO_STAT0_CMDRESPEND)
			{
				ctx->error |= 0x1;
			}
			if(status0 & TMIO_STAT0_DATAEND)
			{
				ctx->error |= 0x2;
			}

			if((status0 & flags) == flags)
				break;
		}
	}
	ctx->stat0 = sdmmc_read16(REG_SDSTATUS0);
	ctx->stat1 = sdmmc_read16(REG_SDSTATUS1);
	sdmmc_write16(REG_SDSTATUS0,0);
	sdmmc_write16(REG_SDSTATUS1,0);

	if(getSDRESP != 0)
	{
		ctx->ret[0] = (u32)(sdmmc_read16(REG_SDRESP0) | (sdmmc_read16(REG_SDRESP1) << 16));
		ctx->ret[1] = (u32)(sdmmc_read16(REG_SDRESP2) | (sdmmc_read16(REG_SDRESP3) << 16));
		ctx->ret[2] = (u32)(sdmmc_read16(REG_SDRESP4) | (sdmmc_read16(REG_SDRESP5) << 16));
		ctx->ret[3] = (u32)(sdmmc_read16(REG_SDRESP6) | (sdmmc_read16(REG_SDRESP7) << 16));
	}
}

//---------------------------------------------------------------------------------
void sdmmc_send_command_nonblocking_ndma(struct mmcdevice *ctx, u32 cmd, u32 args)
//---------------------------------------------------------------------------------
{
	*(u32*)(0x4004104) = 0x0400490C;
	*(u32*)(0x4004108) = (u32)ctx->rData;
	
	*(u32*)(0x400410C) = ctx->size;
	
	*(u32*)(0x4004110) = 0x80;
	
	*(u32*)(0x4004114) = 0x1;
	
	*(u32*)(0x400411C) = 0xC8004000;

	const bool getSDRESP = (cmd << 15) >> 31;
	u16 flags = (cmd << 15) >> 31;
	const bool readdata = cmd & 0x20000;
	const bool writedata = cmd & 0x40000;

	if(readdata || writedata)
	{
		flags |= TMIO_STAT0_DATAEND;
	}

	ctx->error = 0;
	while((sdmmc_read16(REG_SDSTATUS1) & TMIO_STAT1_CMD_BUSY)); //mmc working?
	sdmmc_write16(REG_SDIRMASK0,0);
	sdmmc_write16(REG_SDIRMASK1,0);
	sdmmc_write16(REG_SDSTATUS0,0);
	sdmmc_write16(REG_SDSTATUS1,0);
	sdmmc_mask16(REG_SDDATACTL32,0x1800,0x400); // Disable TX32RQ and RX32RDY IRQ. Clear fifo.
	sdmmc_write16(REG_SDCMDARG0,args &0xFFFF);
	sdmmc_write16(REG_SDCMDARG1,args >> 16);
	sdmmc_write16(REG_SDCMD,cmd &0xFFFF);

	u32 size = ctx->size;
	const u16 blkSize = sdmmc_read16(REG_SDBLKLEN32);
	u32 *rDataPtr32 = (u32*)ctx->rData;
	//u8  *rDataPtr8  = ctx->rData;
	const u32 *tDataPtr32 = (u32*)ctx->tData;
	const u8  *tDataPtr8  = ctx->tData;

	bool rUseBuf = ( NULL != rDataPtr32 );
	bool tUseBuf = ( NULL != tDataPtr32 );

    
	u16 status0 = 0;
	while(1)
	{
		volatile u16 status1 = sdmmc_read16(REG_SDSTATUS1);
#ifdef DATA32_SUPPORT
		volatile u16 ctl32 = sdmmc_read16(REG_SDDATACTL32);
		if((ctl32 & 0x100))
#else
		if((status1 & TMIO_STAT1_RXRDY))
#endif
		{
            /*
            // should not be needed : ndma unit is taking care of data transfer
			if(readdata)
			{
				if(rUseBuf)
				{
					sdmmc_mask16(REG_SDSTATUS1, TMIO_STAT1_RXRDY, 0);
					if(size >= blkSize)
					{
						size -= blkSize;
					}
				}

				sdmmc_mask16(REG_SDDATACTL32, 0x800, 0);
			}*/
		}
#ifdef DATA32_SUPPORT
		if(!(ctl32 & 0x200))
#else
		if((status1 & TMIO_STAT1_TXRQ))
#endif
		{
			if(writedata)
			{
				if(tUseBuf)
				{
					sdmmc_mask16(REG_SDSTATUS1, TMIO_STAT1_TXRQ, 0);
					if(size >= blkSize)
					{
						#ifdef DATA32_SUPPORT
						if(!((u32)tDataPtr32 & 3))
						{
							for(u32 i = 0; i < blkSize; i += 4)
							{
								sdmmc_write32(REG_SDFIFO32, *tDataPtr32++);
							}
						}
						else
						{
							for(u32 i = 0; i < blkSize; i += 4)
							{
								u32 data = *tDataPtr8++;
								data |= (u32)*tDataPtr8++ << 8;
								data |= (u32)*tDataPtr8++ << 16;
								data |= (u32)*tDataPtr8++ << 24;
								sdmmc_write32(REG_SDFIFO32, data);
							}
						}
						#else
						if(!((u32)tDataPtr16 & 1))
						{
							for(u32 i = 0; i < blkSize; i += 2)
							{
								sdmmc_write16(REG_SDFIFO, *tDataPtr16++);
							}
						}
						else
						{
							for(u32 i = 0; i < blkSize; i += 2)
							{
								u16 data = *tDataPtr8++;
								data |= (u16)(*tDataPtr8++ << 8);
								sdmmc_write16(REG_SDFIFO, data);
							}
						}
						#endif
						size -= blkSize;
					}
				}

				sdmmc_mask16(REG_SDDATACTL32, 0x1000, 0);
			}
		}
		if(status1 & TMIO_MASK_GW)
		{
			ctx->error |= 4;
			break;
		}
        
        if(status1 & TMIO_STAT1_CMD_BUSY)
		{
            // command is ongoing : return
            break;
		} 
        else 
        {
            // command is finished already without going busy : return
            // not supposed to happen
            // needed for no$gba only
            /*status0 = sdmmc_read16(REG_SDSTATUS0);
            if((status0 & flags) == flags)
            break;*/
		}		
	}
	//ctx->stat0 = sdmmc_read16(REG_SDSTATUS0);
	//ctx->stat1 = sdmmc_read16(REG_SDSTATUS1);
	//sdmmc_write16(REG_SDSTATUS0,0);
	//sdmmc_write16(REG_SDSTATUS1,0);

	/*if(getSDRESP != 0)
	{
		ctx->ret[0] = (u32)(sdmmc_read16(REG_SDRESP0) | (sdmmc_read16(REG_SDRESP1) << 16));
		ctx->ret[1] = (u32)(sdmmc_read16(REG_SDRESP2) | (sdmmc_read16(REG_SDRESP3) << 16));
		ctx->ret[2] = (u32)(sdmmc_read16(REG_SDRESP4) | (sdmmc_read16(REG_SDRESP5) << 16));
		ctx->ret[3] = (u32)(sdmmc_read16(REG_SDRESP6) | (sdmmc_read16(REG_SDRESP7) << 16));
	}*/
}

/* return true if the command is completed and false if it is still ongoing */
//---------------------------------------------------------------------------------
bool sdmmc_check_command_ndma(struct mmcdevice *ctx, u32 cmd)
//---------------------------------------------------------------------------------
{
	const bool getSDRESP = (cmd << 15) >> 31;
    u16 flags = (cmd << 15) >> 31;
	const bool readdata = cmd & 0x20000;
	const bool writedata = cmd & 0x40000;

	if(readdata || writedata)
	{
		flags |= TMIO_STAT0_DATAEND;
	}    
    u16 status0 = 0;
    
    volatile u16 status1 = sdmmc_read16(REG_SDSTATUS1);
    
    if(status1 & TMIO_MASK_GW)
	{
		ctx->error |= 4;

        ctx->stat0 = sdmmc_read16(REG_SDSTATUS0);
      	ctx->stat1 = sdmmc_read16(REG_SDSTATUS1);
      	sdmmc_write16(REG_SDSTATUS0,0);
      	sdmmc_write16(REG_SDSTATUS1,0);
      
      	if(getSDRESP != 0)
      	{
      		ctx->ret[0] = (u32)(sdmmc_read16(REG_SDRESP0) | (sdmmc_read16(REG_SDRESP1) << 16));
      		ctx->ret[1] = (u32)(sdmmc_read16(REG_SDRESP2) | (sdmmc_read16(REG_SDRESP3) << 16));
      		ctx->ret[2] = (u32)(sdmmc_read16(REG_SDRESP4) | (sdmmc_read16(REG_SDRESP5) << 16));
      		ctx->ret[3] = (u32)(sdmmc_read16(REG_SDRESP6) | (sdmmc_read16(REG_SDRESP7) << 16));
      	}
        *(u32*)(0x400411C) = 0x48004000;
        return true;    
    }        
    
    if(!(status1 & TMIO_STAT1_CMD_BUSY))
	{
		status0 = sdmmc_read16(REG_SDSTATUS0);        
		if(sdmmc_read16(REG_SDSTATUS0) & TMIO_STAT0_CMDRESPEND)
		{
			ctx->error |= 0x1;
		}
		if(status0 & TMIO_STAT0_DATAEND)
		{
			ctx->error |= 0x2;
		}
        
        if((status0 & flags) != flags)
        {
            return false;
        }
		
        ctx->stat0 = sdmmc_read16(REG_SDSTATUS0);
      	ctx->stat1 = sdmmc_read16(REG_SDSTATUS1);
      	sdmmc_write16(REG_SDSTATUS0,0);
      	sdmmc_write16(REG_SDSTATUS1,0);
      
      	if(getSDRESP != 0)
      	{
      		ctx->ret[0] = (u32)(sdmmc_read16(REG_SDRESP0) | (sdmmc_read16(REG_SDRESP1) << 16));
      		ctx->ret[1] = (u32)(sdmmc_read16(REG_SDRESP2) | (sdmmc_read16(REG_SDRESP3) << 16));
      		ctx->ret[2] = (u32)(sdmmc_read16(REG_SDRESP4) | (sdmmc_read16(REG_SDRESP5) << 16));
      		ctx->ret[3] = (u32)(sdmmc_read16(REG_SDRESP6) | (sdmmc_read16(REG_SDRESP7) << 16));
      	}
        *(u32*)(0x400411C) = 0x48004000; 
        return true;
	} else return false;        
}

//---------------------------------------------------------------------------------
void sdmmc_send_command_ndma(struct mmcdevice *ctx, u32 cmd, u32 args)
//---------------------------------------------------------------------------------
{
	*(u32*)(0x4004104) = 0x0400490C;
	*(u32*)(0x4004108) = (u32)ctx->rData;
	
	*(u32*)(0x400410C) = ctx->size;
	
	*(u32*)(0x4004110) = 0x80;
	
	*(u32*)(0x4004114) = 0x1;
	
	*(u32*)(0x400411C) = 0xC8004000;


	const bool getSDRESP = (cmd << 15) >> 31;
	u16 flags = (cmd << 15) >> 31;
	const bool readdata = cmd & 0x20000;
	const bool writedata = cmd & 0x40000;

	if(readdata || writedata)
	{
		flags |= TMIO_STAT0_DATAEND;
	}

	ctx->error = 0;
	while((sdmmc_read16(REG_SDSTATUS1) & TMIO_STAT1_CMD_BUSY)); //mmc working?
	sdmmc_write16(REG_SDIRMASK0,0);
	sdmmc_write16(REG_SDIRMASK1,0);
	sdmmc_write16(REG_SDSTATUS0,0);
	sdmmc_write16(REG_SDSTATUS1,0);
	sdmmc_mask16(REG_SDDATACTL32,0x1800,0x400); // Disable TX32RQ and RX32RDY IRQ. Clear fifo.
	sdmmc_write16(REG_SDCMDARG0,args &0xFFFF);
	sdmmc_write16(REG_SDCMDARG1,args >> 16);
	sdmmc_write16(REG_SDCMD,cmd &0xFFFF);

	u32 size = ctx->size;
	const u16 blkSize = sdmmc_read16(REG_SDBLKLEN32);
	u32 *rDataPtr32 = (u32*)ctx->rData;
	//u8  *rDataPtr8  = ctx->rData;
	const u32 *tDataPtr32 = (u32*)ctx->tData;
	const u8  *tDataPtr8  = ctx->tData;

	bool rUseBuf = ( NULL != rDataPtr32 );
	bool tUseBuf = ( NULL != tDataPtr32 );

	u16 status0 = 0;
	while(1)
	{
		volatile u16 status1 = sdmmc_read16(REG_SDSTATUS1);
#ifdef DATA32_SUPPORT
		volatile u16 ctl32 = sdmmc_read16(REG_SDDATACTL32);
		if((ctl32 & 0x100))
#else
		if((status1 & TMIO_STAT1_RXRDY))
#endif
		{
			if(readdata)
			{
				if(rUseBuf)
				{
					sdmmc_mask16(REG_SDSTATUS1, TMIO_STAT1_RXRDY, 0);
					if(size >= blkSize)
					{
						#ifdef DATA32_SUPPORT
						if(!((u32)rDataPtr32 & 3))
						{
							//for(u32 i = 0; i < blkSize; i += 4)
							//{
							//	*rDataPtr32++ = sdmmc_read32(REG_SDFIFO32);
							//}
						}
						else
						{
							//for(u32 i = 0; i < blkSize; i += 4)
							//{
							//	u32 data = sdmmc_read32(REG_SDFIFO32);
							//	*rDataPtr8++ = data;
							//	*rDataPtr8++ = data >> 8;
							//	*rDataPtr8++ = data >> 16;
							//	*rDataPtr8++ = data >> 24;
							//}
						}
						#else
						if(!((u32)rDataPtr16 & 1))
						{
							//for(u32 i = 0; i < blkSize; i += 4)
							//{
							//	*rDataPtr16++ = sdmmc_read16(REG_SDFIFO);
							//}
						}
						else
						{
							//for(u32 i = 0; i < blkSize; i += 4)
							//{
							//	u16 data = sdmmc_read16(REG_SDFIFO);
							//	*rDataPtr8++ = data;
							//	*rDataPtr8++ = data >> 8;
							//}
						}
						#endif
						size -= blkSize;
					}
				}

				sdmmc_mask16(REG_SDDATACTL32, 0x800, 0);
			}
		}
#ifdef DATA32_SUPPORT
		if(!(ctl32 & 0x200))
#else
		if((status1 & TMIO_STAT1_TXRQ))
#endif
		{
			if(writedata)
			{
				if(tUseBuf)
				{
					sdmmc_mask16(REG_SDSTATUS1, TMIO_STAT1_TXRQ, 0);
					if(size >= blkSize)
					{
						#ifdef DATA32_SUPPORT
						if(!((u32)tDataPtr32 & 3))
						{
							for(u32 i = 0; i < blkSize; i += 4)
							{
								sdmmc_write32(REG_SDFIFO32, *tDataPtr32++);
							}
						}
						else
						{
							for(u32 i = 0; i < blkSize; i += 4)
							{
								u32 data = *tDataPtr8++;
								data |= (u32)*tDataPtr8++ << 8;
								data |= (u32)*tDataPtr8++ << 16;
								data |= (u32)*tDataPtr8++ << 24;
								sdmmc_write32(REG_SDFIFO32, data);
							}
						}
						#else
						if(!((u32)tDataPtr16 & 1))
						{
							for(u32 i = 0; i < blkSize; i += 2)
							{
								sdmmc_write16(REG_SDFIFO, *tDataPtr16++);
							}
						}
						else
						{
							for(u32 i = 0; i < blkSize; i += 2)
							{
								u16 data = *tDataPtr8++;
								data |= (u16)(*tDataPtr8++ << 8);
								sdmmc_write16(REG_SDFIFO, data);
							}
						}
						#endif
						size -= blkSize;
					}
				}

				sdmmc_mask16(REG_SDDATACTL32, 0x1000, 0);
			}
		}
		if(status1 & TMIO_MASK_GW)
		{
			ctx->error |= 4;
			break;
		}

		if(!(status1 & TMIO_STAT1_CMD_BUSY))
		{
			status0 = sdmmc_read16(REG_SDSTATUS0);
			if(sdmmc_read16(REG_SDSTATUS0) & TMIO_STAT0_CMDRESPEND)
			{
				ctx->error |= 0x1;
			}
			if(status0 & TMIO_STAT0_DATAEND)
			{
				ctx->error |= 0x2;
			}

			if((status0 & flags) == flags)
				break;
		}
	}
	ctx->stat0 = sdmmc_read16(REG_SDSTATUS0);
	ctx->stat1 = sdmmc_read16(REG_SDSTATUS1);
	sdmmc_write16(REG_SDSTATUS0,0);
	sdmmc_write16(REG_SDSTATUS1,0);

	if(getSDRESP != 0)
	{
		ctx->ret[0] = (u32)(sdmmc_read16(REG_SDRESP0) | (sdmmc_read16(REG_SDRESP1) << 16));
		ctx->ret[1] = (u32)(sdmmc_read16(REG_SDRESP2) | (sdmmc_read16(REG_SDRESP3) << 16));
		ctx->ret[2] = (u32)(sdmmc_read16(REG_SDRESP4) | (sdmmc_read16(REG_SDRESP5) << 16));
		ctx->ret[3] = (u32)(sdmmc_read16(REG_SDRESP6) | (sdmmc_read16(REG_SDRESP7) << 16));
	}
	*(u32*)(0x400411C) = 0x48004000;
}


//---------------------------------------------------------------------------------
int sdmmc_cardinserted() {
//---------------------------------------------------------------------------------
	return 1; //sdmmc_cardready;
}


static bool sdmmc_controller_initialised = false;

//---------------------------------------------------------------------------------
void sdmmc_controller_init( bool force_init ) {
//---------------------------------------------------------------------------------

    if (!force_init && sdmmc_controller_initialised) return;

    deviceSD.isSDHC = 0;
    deviceSD.SDOPT = 0;
    deviceSD.res = 0;
    deviceSD.initarg = 0;
    deviceSD.clk = 0x80;
    deviceSD.devicenumber = 0;

    deviceNAND.isSDHC = 0;
    deviceNAND.SDOPT = 0;
    deviceNAND.res = 0;
    deviceNAND.initarg = 1;
    deviceNAND.clk = 0x80;
    deviceNAND.devicenumber = 1;

    *(vu16*)(SDMMC_BASE + REG_SDDATACTL32) &= 0xF7FFu;
    *(vu16*)(SDMMC_BASE + REG_SDDATACTL32) &= 0xEFFFu;
#ifdef DATA32_SUPPORT
    *(vu16*)(SDMMC_BASE + REG_SDDATACTL32) |= 0x402u;
#else
    *(vu16*)(SDMMC_BASE + REG_SDDATACTL32) |= 0x402u;
#endif
    *(vu16*)(SDMMC_BASE + REG_SDDATACTL) = (*(vu16*)(SDMMC_BASE + REG_SDDATACTL) & 0xFFDD) | 2;
#ifdef DATA32_SUPPORT
    *(vu16*)(SDMMC_BASE + REG_SDDATACTL32) &= 0xFFFFu;
    *(vu16*)(SDMMC_BASE + REG_SDDATACTL) &= 0xFFDFu;
    *(vu16*)(SDMMC_BASE + REG_SDBLKLEN32) = 512;
#else
    *(vu16*)(SDMMC_BASE + REG_SDDATACTL32) &= 0xFFFDu;
    *(vu16*)(SDMMC_BASE + REG_SDDATACTL) &= 0xFFDDu;
    *(vu16*)(SDMMC_BASE + REG_SDBLKLEN32) = 0;
#endif
    *(vu16*)(SDMMC_BASE + REG_SDBLKCOUNT32) = 1;
    *(vu16*)(SDMMC_BASE + REG_SDRESET) &= 0xFFFEu;
    *(vu16*)(SDMMC_BASE + REG_SDRESET) |= 1u;
    *(vu16*)(SDMMC_BASE + REG_SDIRMASK0) |= TMIO_MASK_ALL;
    *(vu16*)(SDMMC_BASE + REG_SDIRMASK1) |= TMIO_MASK_ALL>>16;
    *(vu16*)(SDMMC_BASE + 0x0fc) |= 0xDBu; //SDCTL_RESERVED7
    *(vu16*)(SDMMC_BASE + 0x0fe) |= 0xDBu; //SDCTL_RESERVED8
    *(vu16*)(SDMMC_BASE + REG_SDPORTSEL) &= 0xFFFCu;
#ifdef DATA32_SUPPORT
    *(vu16*)(SDMMC_BASE + REG_SDCLKCTL) = 0x20;
    *(vu16*)(SDMMC_BASE + REG_SDOPT) = 0x40EE;
#else
    *(vu16*)(SDMMC_BASE + REG_SDCLKCTL) = 0x40; //Nintendo sets this to 0x20
    *(vu16*)(SDMMC_BASE + REG_SDOPT) = 0x40EB; //Nintendo sets this to 0x40EE
#endif
    *(vu16*)(SDMMC_BASE + REG_SDPORTSEL) &= 0xFFFCu;
    *(vu16*)(SDMMC_BASE + REG_SDBLKLEN) = 512;
    *(vu16*)(SDMMC_BASE + REG_SDSTOP) = 0;

    sdmmc_controller_initialised = true;

    setTarget(&deviceSD);
}

//---------------------------------------------------------------------------------
static u32 calcSDSize(u8* csd, int type) {
//---------------------------------------------------------------------------------
    u32 result = 0;
    if (type == -1) type = csd[14] >> 6;
    switch (type) {
        case 0:
            {
                u32 block_len = csd[9] & 0xf;
                block_len = 1 << block_len;
                u32 mult = (csd[4] >> 7) | ((csd[5] & 3) << 1);
                mult = 1 << (mult + 2);
                result = csd[8] & 3;
                result = (result << 8) | csd[7];
                result = (result << 2) | (csd[6] >> 6);
                result = (result + 1) * mult * block_len / 512;
            }
            break;
        case 1:
            result = csd[7] & 0x3f;
            result = (result << 8) | csd[6];
            result = (result << 8) | csd[5];
            result = (result + 1) * 1024;
            break;
    }
    return result;
}

//---------------------------------------------------------------------------------
int sdmmc_sdcard_init() {
//---------------------------------------------------------------------------------
	// We need to send at least 74 clock pulses.
    setTarget(&deviceSD);
	swiDelay(0x1980); // ~75-76 clocks

    // card reset
    sdmmc_send_command(&deviceSD,0,0);

    // CMD8 0x1AA
    sdmmc_send_command(&deviceSD,0x10408,0x1AA);
    u32 temp = (deviceSD.error & 0x1) << 0x1E;

    u32 temp2 = 0;
    do {
        do {
            // CMD55
            sdmmc_send_command(&deviceSD,0x10437,deviceSD.initarg << 0x10);
            // ACMD41
            sdmmc_send_command(&deviceSD,0x10769,0x00FF8000 | temp);
            temp2 = 1;
        } while ( !(deviceSD.error & 1) );

    } while((deviceSD.ret[0] & 0x80000000) == 0);

    if(!((deviceSD.ret[0] >> 30) & 1) || !temp)
        temp2 = 0;

    deviceSD.isSDHC = temp2;

    sdmmc_send_command(&deviceSD,0x10602,0);
    if (deviceSD.error & 0x4) return -1;

    sdmmc_send_command(&deviceSD,0x10403,0);
    if (deviceSD.error & 0x4) return -2;
    deviceSD.initarg = deviceSD.ret[0] >> 0x10;

    sdmmc_send_command(&deviceSD,0x10609,deviceSD.initarg << 0x10);
    if (deviceSD.error & 0x4) return -3;

	// Command Class 10 support
	const bool cmd6Supported = ((u8*)deviceSD.ret)[10] & 0x40;
    deviceSD.total_size = calcSDSize((u8*)&deviceSD.ret[0],-1);
    setckl(0x201); // 16.756991 MHz

    sdmmc_send_command(&deviceSD,0x10507,deviceSD.initarg << 0x10);
    if (deviceSD.error & 0x4) return -4;

    // CMD55
    sdmmc_send_command(&deviceSD,0x10437,deviceSD.initarg << 0x10);
    if (deviceSD.error & 0x4) return -5;

    // ACMD42
    sdmmc_send_command(&deviceSD,0x1076A,0x0);
    if (deviceSD.error & 0x4) return -6;

    // CMD55
    sdmmc_send_command(&deviceSD,0x10437,deviceSD.initarg << 0x10);
    if (deviceSD.error & 0x4) return -7;

    deviceSD.SDOPT = 1;
    sdmmc_send_command(&deviceSD,0x10446,0x2);
    if (deviceSD.error & 0x4) return -8;
	sdmmc_mask16(REG_SDOPT, 0x8000, 0); // Switch to 4 bit mode.

	// TODO: CMD6 to switch to high speed mode.
	if(cmd6Supported)
	{
		sdmmc_write16(REG_SDSTOP,0);
		sdmmc_write16(REG_SDBLKLEN32,64);
		sdmmc_write16(REG_SDBLKLEN,64);
		deviceSD.rData = NULL;
		deviceSD.size = 64;
		sdmmc_send_command(&deviceSD,0x31C06,0x80FFFFF1);
		sdmmc_write16(REG_SDBLKLEN,512);
		if(deviceSD.error & 0x4) return -9;

		deviceSD.clk = 0x200; // 33.513982 MHz
		setckl(0x200);
	}
	else deviceSD.clk = 0x201; // 16.756991 MHz

    sdmmc_send_command(&deviceSD,0x1040D,deviceSD.initarg << 0x10);
    if (deviceSD.error & 0x4) return -9;

    sdmmc_send_command(&deviceSD,0x10410,0x200);
    if (deviceSD.error & 0x4) return -10;

    return 0;

}

//---------------------------------------------------------------------------------
int sdmmc_nand_init() {
//---------------------------------------------------------------------------------
    setTarget(&deviceNAND);
    swiDelay(0xF000);

    sdmmc_send_command(&deviceNAND,0,0);

    do {
        do {
            sdmmc_send_command(&deviceNAND,0x10701,0x100000);
        } while ( !(deviceNAND.error & 1) );
    }
    while((deviceNAND.ret[0] & 0x80000000) == 0);

    sdmmc_send_command(&deviceNAND,0x10602,0x0);
    if((deviceNAND.error & 0x4))return -1;

    sdmmc_send_command(&deviceNAND,0x10403,deviceNAND.initarg << 0x10);
    if((deviceNAND.error & 0x4))return -1;

    sdmmc_send_command(&deviceNAND,0x10609,deviceNAND.initarg << 0x10);
    if((deviceNAND.error & 0x4))return -1;

    deviceNAND.total_size = calcSDSize((uint8_t*)&deviceNAND.ret[0],0);
    deviceNAND.clk = 1;
    setckl(1);

    sdmmc_send_command(&deviceNAND,0x10407,deviceNAND.initarg << 0x10);
    if((deviceNAND.error & 0x4))return -1;

    deviceNAND.SDOPT = 1;

    sdmmc_send_command(&deviceNAND,0x10506,0x3B70100);
    if((deviceNAND.error & 0x4))return -1;

    sdmmc_send_command(&deviceNAND,0x10506,0x3B90100);
    if((deviceNAND.error & 0x4))return -1;

    sdmmc_send_command(&deviceNAND,0x1040D,deviceNAND.initarg << 0x10);
    if((deviceNAND.error & 0x4))return -1;

    sdmmc_send_command(&deviceNAND,0x10410,0x200);
    if((deviceNAND.error & 0x4))return -1;

    deviceNAND.clk |= 0x200;

    setTarget(&deviceSD);

    return 0;
}

//---------------------------------------------------------------------------------
int sdmmc_readsectors(struct mmcdevice *device, u32 sector_no, u32 numsectors, void *out) {
//---------------------------------------------------------------------------------
    if (device->isSDHC == 0) sector_no <<= 9;
    setTarget(device);
    sdmmc_write16(REG_SDSTOP,0x100);

#ifdef DATA32_SUPPORT
    sdmmc_write16(REG_SDBLKCOUNT32,numsectors);
    sdmmc_write16(REG_SDBLKLEN32,0x200);
#endif

    sdmmc_write16(REG_SDBLKCOUNT,numsectors);
    device->rData = out;
    device->size = numsectors << 9;
    device->startOffset = 0;
    device->endOffset = 0;
    //sdmmc_send_command(device,0x33C12,sector_no);
    sdmmc_send_command_nonblocking_ndma(device,0x33C12,sector_no);
    while(!sdmmc_check_command_ndma(device,0x33C12)) {}
    setTarget(&deviceSD);
    return geterror(device);
}

//---------------------------------------------------------------------------------
int sdmmc_writesectors(struct mmcdevice *device, u32 sector_no, u32 numsectors, void *in) {
//---------------------------------------------------------------------------------
    if (device->isSDHC == 0)
        sector_no <<= 9;
    setTarget(device);
    sdmmc_write16(REG_SDSTOP,0x100);

#ifdef DATA32_SUPPORT
    sdmmc_write16(REG_SDBLKCOUNT32,numsectors);
    sdmmc_write16(REG_SDBLKLEN32,0x200);
#endif

    sdmmc_write16(REG_SDBLKCOUNT,numsectors);
    device->tData = in;
    device->size = numsectors << 9;
    device->startOffset = 0;
    device->endOffset = 0;
    sdmmc_send_command(device,0x52C19,sector_no);
    setTarget(&deviceSD);
    return geterror(device);
}

//---------------------------------------------------------------------------------
void sdmmc_get_cid(int devicenumber, u32 *cid) {
//---------------------------------------------------------------------------------

    struct mmcdevice *device = (devicenumber == 1 ? &deviceNAND : &deviceSD);

    int oldIME = enterCriticalSection();

    setTarget(device);

    // use cmd7 to put sd card in standby mode
    // CMD7
    sdmmc_send_command(device, 0x10507, 0);

    // get sd card info
    // use cmd10 to read CID
    sdmmc_send_command(device, 0x1060A, device->initarg << 0x10);

    for(int i = 0; i < 4; ++i)
        cid[i] = device->ret[i];

    // put sd card back to transfer mode
    // CMD7
    sdmmc_send_command(device, 0x10507, device->initarg << 0x10);

    leaveCriticalSection(oldIME);
}

//---------------------------------------------------------------------------------
void sdmmcMsgHandler(int bytes, void *user_data) {
//---------------------------------------------------------------------------------
    FifoMessage msg;
    int retval = 0;

    fifoGetDatamsg(FIFO_SDMMC, bytes, (u8*)&msg);

    int oldIME = enterCriticalSection();
    switch (msg.type) {

    case SDMMC_SD_READ_SECTORS:
        retval = sdmmc_readsectors(&deviceSD, msg.sdParams.startsector, msg.sdParams.numsectors, msg.sdParams.buffer);
        break;
    case SDMMC_SD_WRITE_SECTORS:
        retval = sdmmc_writesectors(&deviceSD, msg.sdParams.startsector, msg.sdParams.numsectors, msg.sdParams.buffer);
        break;
    case SDMMC_NAND_READ_SECTORS:
        retval = sdmmc_readsectors(&deviceNAND, msg.sdParams.startsector, msg.sdParams.numsectors, msg.sdParams.buffer);
        break;
    case SDMMC_NAND_WRITE_SECTORS:
        retval = sdmmc_writesectors(&deviceNAND, msg.sdParams.startsector, msg.sdParams.numsectors, msg.sdParams.buffer);
        break;
    }

    leaveCriticalSection(oldIME);

    fifoSendValue32(FIFO_SDMMC, retval);
}

//---------------------------------------------------------------------------------
int sdmmc_nand_startup() {
//---------------------------------------------------------------------------------
    sdmmc_controller_init(false);
    return sdmmc_nand_init();
}

//---------------------------------------------------------------------------------
int sdmmc_sd_startup() {
//---------------------------------------------------------------------------------
    sdmmc_controller_init(false);
    return sdmmc_sdcard_init();
}

//---------------------------------------------------------------------------------
void sdmmcValueHandler(u32 value, void* user_data) {
//---------------------------------------------------------------------------------
    int result = 0;
    int sdflag = 0;
    int oldIME = enterCriticalSection();

    switch(value) {

    case SDMMC_HAVE_SD:
        result = sdmmc_read16(REG_SDSTATUS0);
        break;

    case SDMMC_SD_START:
        sdflag = 1;
        /* Falls through. */
    case SDMMC_NAND_START:
        if (sdmmc_read16(REG_SDSTATUS0) == 0) {
            result = 1;
        } else {
            result = (sdflag == 1 ) ? sdmmc_sd_startup() : sdmmc_nand_startup();
        }
        break;

    case SDMMC_SD_IS_INSERTED:
        result = sdmmc_cardinserted();
        break;

    case SDMMC_SD_STOP:
        break;

    case SDMMC_NAND_SIZE:
        result = deviceNAND.total_size;
        break;
    }

    leaveCriticalSection(oldIME);

    fifoSendValue32(FIFO_SDMMC, result);
}

//---------------------------------------------------------------------------------
int sdmmc_sdcard_readsectors(u32 sector_no, u32 numsectors, void *out) {
//---------------------------------------------------------------------------------
    return sdmmc_readsectors(&deviceSD, sector_no, numsectors, out);
}

//---------------------------------------------------------------------------------
int sdmmc_sdcard_writesectors(u32 sector_no, u32 numsectors, void *in) {
//---------------------------------------------------------------------------------
    return sdmmc_writesectors(&deviceSD, sector_no, numsectors, in);
}

//---------------------------------------------------------------------------------
int sdmmc_nand_readsectors(u32 sector_no, u32 numsectors, void *out) {
//---------------------------------------------------------------------------------
    return sdmmc_readsectors(&deviceNAND, sector_no, numsectors, out);
}

//---------------------------------------------------------------------------------
int sdmmc_nand_writesectors(u32 sector_no, u32 numsectors, void *in) {
//---------------------------------------------------------------------------------
    return sdmmc_writesectors(&deviceNAND, sector_no, numsectors, in);
}


