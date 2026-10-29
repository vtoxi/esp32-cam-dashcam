import { Component, OnInit, inject, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { DataTableColumn, DataTableComponent } from '../../shared/components/data-table/data-table.component';
import { CellTemplateDirective } from '../../shared/components/data-table/cell-template.directive';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { JsonViewerComponent } from '../../shared/components/json-viewer/json-viewer.component';
import { DevicePickerComponent } from '../../shared/components/device-picker/device-picker.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { TelemetryRecord } from '../../core/models';
import { DeviceService } from '../../core/services/device.service';

@Component({
  selector: 'app-telemetry-list',
  standalone: true,
  imports: [
    RouterLink,
    DataTableComponent,
    CellTemplateDirective,
    PageHeaderComponent,
    JsonViewerComponent,
    DevicePickerComponent,
    IconComponent,
    RelativeTimePipe,
  ],
  templateUrl: './telemetry-list.component.html',
})
export class TelemetryListComponent implements OnInit {
  private readonly deviceService = inject(DeviceService);

  readonly loading = signal(true);
  readonly rows = signal<TelemetryRecord[]>([]);
  readonly selectedDevice = signal<string | null>(null);

  readonly columns: DataTableColumn<TelemetryRecord>[] = [
    { key: 'deviceId', header: 'Device' },
    { key: 'receivedAt', header: 'Received' },
    { key: 'payloadJson', header: 'Payload', sortable: false },
  ];

  ngOnInit(): void {
    this.load();
  }

  onDeviceChange(deviceId: string | null): void {
    this.selectedDevice.set(deviceId);
    this.load();
  }

  load(): void {
    this.loading.set(true);
    const deviceId = this.selectedDevice();
    if (deviceId) {
      this.deviceService.telemetry(deviceId, 500).subscribe((rows) => {
        this.rows.set(rows);
        this.loading.set(false);
      });
    } else {
      // No global telemetry endpoint exists (only /devices/{id}/telemetry) —
      // "All devices" merges every device's most recent telemetry client-side,
      // which is fine at reference-deployment scale and keeps this page useful
      // without a backend change purely for the dashboard's sake.
      this.deviceService.list().subscribe((devices) => {
        if (devices.length === 0) {
          this.rows.set([]);
          this.loading.set(false);
          return;
        }
        let remaining = devices.length;
        const collected: TelemetryRecord[] = [];
        devices.forEach((d) => {
          this.deviceService.telemetry(d.id, 50).subscribe((rows) => {
            collected.push(...rows);
            remaining--;
            if (remaining === 0) {
              this.rows.set(collected.sort((a, b) => b.receivedAt.localeCompare(a.receivedAt)));
              this.loading.set(false);
            }
          });
        });
      });
    }
  }

  readonly rowKeyFn = (r: TelemetryRecord) => `${r.deviceId}-${r.id}`;
}
