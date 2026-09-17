import { Component, Input, OnInit, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import type { DialogRef } from '../../shared/components/dialog/dialog.service';
import { DialogShellComponent } from '../../shared/components/dialog/dialog-shell.component';
import { Device } from '../../core/models';
import { DeviceService } from '../../core/services/device.service';

/** Admin-gated edit of the one field this backend never sets on a device's own
 * behalf — tenantId (docs/BACKEND.md's multi-tenant-readiness note). Every other
 * Device field is firmware-reported and would just be overwritten on the next
 * register/heartbeat, so there's nothing else here worth an admin edit for. */
@Component({
  selector: 'app-edit-device-dialog',
  standalone: true,
  imports: [FormsModule, DialogShellComponent],
  templateUrl: './edit-device-dialog.component.html',
})
export class EditDeviceDialogComponent implements OnInit {
  @Input({ required: true }) device!: Device;
  dialogRef!: DialogRef<boolean>;

  private readonly deviceService = inject(DeviceService);
  tenantId = '';
  readonly submitting = signal(false);

  ngOnInit(): void {
    this.tenantId = this.device.tenantId ?? '';
  }

  submit(): void {
    this.submitting.set(true);
    this.deviceService.updateTenant(this.device.id, this.tenantId.trim() || null).subscribe({
      next: () => {
        this.submitting.set(false);
        this.dialogRef.close(true);
      },
      error: () => this.submitting.set(false),
    });
  }
}
