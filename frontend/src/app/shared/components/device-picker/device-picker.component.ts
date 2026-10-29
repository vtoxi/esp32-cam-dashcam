import { Component, EventEmitter, Input, OnInit, Output, inject, signal } from '@angular/core';
import { Device } from '../../../core/models';
import { DeviceService } from '../../../core/services/device.service';

/** A device <select>, used by every page scoped to "one device's data"
 * (Telemetry, Events, Commands) — loads the device list once, shared here
 * instead of duplicated per page. */
@Component({
  selector: 'app-device-picker',
  standalone: true,
  template: `
    <select
      class="focus-ring rounded-lg border border-white/10 bg-surface-200/60 px-3 py-2 text-sm text-slate-200 min-w-[220px]"
      [value]="value ?? ''"
      (change)="onChange($any($event.target).value)"
    >
      <option value="">{{ allLabel }}</option>
      @for (d of devices(); track d.id) {
        <option [value]="d.id">{{ d.id }}</option>
      }
    </select>
  `,
})
export class DevicePickerComponent implements OnInit {
  @Input() value: string | null = null;
  @Input() allLabel = 'All devices';
  @Output() valueChange = new EventEmitter<string | null>();

  private readonly deviceService = inject(DeviceService);
  readonly devices = signal<Device[]>([]);

  ngOnInit(): void {
    this.deviceService.list().subscribe((devices) => this.devices.set(devices));
  }

  onChange(value: string): void {
    this.valueChange.emit(value || null);
  }
}
