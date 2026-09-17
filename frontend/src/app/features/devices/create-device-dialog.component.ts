import { Component, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import type { DialogRef } from '../../shared/components/dialog/dialog.service';
import { DialogShellComponent } from '../../shared/components/dialog/dialog-shell.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { CopyFieldComponent } from '../../shared/components/copy-field/copy-field.component';
import { DeviceService, PreProvisionDeviceResponse } from '../../core/services/device.service';

/**
 * Pre-provisions a device (POST /api/v1/devices, admin-gated) and shows the
 * generated credential exactly once — the operator must copy both the device ID
 * and credential into the physical gateway's own Settings page (Remote Backend
 * section) or BACKENDCONFIG serial command before it can register as this device.
 */
@Component({
  selector: 'app-create-device-dialog',
  standalone: true,
  imports: [FormsModule, DialogShellComponent, IconComponent, CopyFieldComponent],
  templateUrl: './create-device-dialog.component.html',
})
export class CreateDeviceDialogComponent {
  dialogRef!: DialogRef<boolean>;

  private readonly deviceService = inject(DeviceService);

  hardwareProfile = '';
  nodeId = '';
  tenantId = '';

  readonly submitting = signal(false);
  readonly error = signal<string | null>(null);
  readonly created = signal<PreProvisionDeviceResponse | null>(null);

  submit(): void {
    this.error.set(null);
    this.submitting.set(true);
    this.deviceService
      .preProvision({
        hardwareProfile: this.hardwareProfile.trim() || undefined,
        nodeId: this.nodeId.trim() || undefined,
        tenantId: this.tenantId.trim() || undefined,
      })
      .subscribe({
        next: (result) => {
          this.submitting.set(false);
          this.created.set(result);
        },
        error: () => this.submitting.set(false),
      });
  }

  done(): void {
    this.dialogRef.close(true);
  }
}
